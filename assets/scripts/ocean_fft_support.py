import argparse
import math
from dataclasses import dataclass
from io import StringIO
from pathlib import Path

import numpy as np
import yaml

"""
Generates the OceanFft support data from OceanFftConfig.yaml, as a DiamondDogs build step:
- OceanFftGenerated.slang: the Mitsuyasu and Hasselmann Q(s) polynomial fits, and the Donelan-Banner LUT sampling
  function. It is written next to Spectra.slang, which includes it (the cooker only searches the module's folder).
- DonelanBannerNormLUT.hpp/.cpp: the Donelan-Banner normalization LUT, R32_SFLOAT, x = beta, y = sqrt-mapped swell s.
- SinCosLUT.hpp/.cpp: e^(-i theta) for theta = 2 pi i / N, packed f16x2 (R16G16_SFLOAT), N = the largest IFFT size.

Only numpy and pyyaml are needed. This file is the canonical copy; VeloxRhi syncs from it.

TODO:
- Start layering the 2D LUT as a 3D lut, with parameter ranges and resolution being the mutation parameters, so that we can have a single
  LUT that covers the entire range of expected physical parameters, and then we choose layer to sample on based on physical params and perf settings
- Maybe consider passing polynomial coefficients in through a CBV, but would restrict us to degree 3. Does mean less reliance on generated code,
  which is always brittle and a sneaky point of failure especially when accurate math is important
"""

# the smallest s the polynomial fits cover. Q(s) is flat (1 / 2 pi) below it, and log(0) has no fit
S_FLOOR = 1.0e-4

# the quadrature's half-width in units of the swell lobe's width: cos(theta / 2)^(2 s) ~ exp(-s theta^2 / 4), whose
# standard deviation is sqrt(2 / s), so 10 / sqrt(s) is about 7 standard deviations
SWELL_WINDOW_SCALE = 10.0

# the largest IFFT size the shaders run. The sin/cos LUT is built for it, and smaller sizes read every (N / size)th entry
MAX_IFFT_SIZE = 1024


@dataclass
class ValueBounds:
    min: float
    max: float


@dataclass
class LUTParameters:
    resolution: int
    use_f16: bool


def donelan_banner_beta(omega_ratio: np.ndarray) -> np.ndarray:
    """ Donelan-Banner beta for omega / peakOmega, the same branches as DonelanBannerBeta in Spectra.slang """
    x = np.asarray(omega_ratio, dtype=np.float64)
    high = 10.0 ** (-0.4 + 0.8393 * np.exp(-0.567 * np.log(x * x)))
    return np.where(x < 0.56, 1.24, np.where(x < 0.95, 2.61 * x ** 1.3, np.where(x < 1.6, 2.28 * x ** -1.3, high)))


def calculate_parameter_space(wind_speed_bounds: ValueBounds,
                              fetch_length_bounds: ValueBounds,
                              swell_bounds: ValueBounds,
                              ratio_min: float, ratio_max: float,
                              axis_resolution=64) -> tuple:
    """
    Calculates the parameter space for the directional spreading function based on the given maximum wind speed, fetch, swell, and ratio range.
    We know these physical parameters have an explicit relationship to S, and that we'll have limits on them based on the design of the game
    and ocean simulation.
    @param wind_speed_bounds: Bounds for wind speed (m/s)
    @param fetch_length_bounds: Bounds for fetch (km)
    @param swell_bounds: Bounds for swell (m)
    @param ratio_min: Minimum ratio
    @param ratio_max: Maximum ratio

    @return: A tuple containing ValueBounds objects for Hasselman S, Mitsuyasu S, Donnelan-Banner Beta, and Swell S, respectively.
    """

    # TODO: Consider making axis resolution work in log space relative to the min/max values, as the actual size of the
    # parameter range varies for each one. Swell is small, but fetch and wind speed can be large
    wind_speed_space = np.linspace(wind_speed_bounds.min, wind_speed_bounds.max, axis_resolution)
    fetch_space = np.linspace(fetch_length_bounds.min, fetch_length_bounds.max, axis_resolution)
    swell_space = np.linspace(swell_bounds.min, swell_bounds.max, axis_resolution)
    # The important factor shaping the spreading is the ratio of omega to peakOmega, so thankfully we can just evaluate the ratio space directly
    breakpoints = np.array([b for b in (0.56, float(np.nextafter(0.95, 0.0)), 0.95, 1.0, 1.6) if ratio_min <= b <= ratio_max], dtype=np.float64)
    omega_ratio_space = np.unique(np.concatenate([np.linspace(ratio_min, ratio_max, axis_resolution), breakpoints]))

    wind_speed_grid, fetch_grid, swell_grid, omega_ratio_grid = np.meshgrid(wind_speed_space, fetch_space, swell_space, omega_ratio_space, indexing='ij')

    # Calculate the peak frequency (peakOmega) for JONSWAP based on inputs
    # chi = dimensionless fetch
    g = 9.81
    # important note: fetch_grid is in km, so we multiply by 1000 to convert to m for the dimensionless fetch calculation!
    chi = np.minimum((g * fetch_grid * 1000.0) / (wind_speed_grid ** 2), 1.0e6)
    nu_peak = 3.5 * (chi ** -0.33)
    peakOmega = 2 * np.pi * nu_peak * g / wind_speed_grid

    # Swell s - additive factor to the spreading function based on swell
    s_swell = np.minimum(16.1 * np.tanh(1.0 / omega_ratio_grid) * (swell_grid**2), 16.0)
    s_swell_bounds = ValueBounds(max(0.0, float(np.min(s_swell))), float(np.max(s_swell)))

    # Hasselman s - the spreading factor based on the Hasselman model
    mu = -2.33 - 1.45 * ((wind_speed_grid * peakOmega / g) - 1.17)
    s_hasselman = np.where(omega_ratio_grid <= 1.0, 6.97 * (omega_ratio_grid ** 4.06), 9.77 * (omega_ratio_grid ** mu))
    s_hasselman_total = s_hasselman + s_swell
    s_hasselman_bounds = ValueBounds(float(np.min(s_hasselman_total)), float(np.max(s_hasselman_total)))

    # Mitsuyasu s - the spreading factor based on the Mitsuyasu model
    s_mitsuyasu_base = 11.5 * ((peakOmega * wind_speed_grid / g) ** -2.5)
    s_mitsuyasu = np.where(omega_ratio_grid <= 1.0,
                           s_mitsuyasu_base * (omega_ratio_grid ** 5.0),
                           s_mitsuyasu_base * (omega_ratio_grid ** -2.5))
    s_mitsuyasu_total = s_mitsuyasu + s_swell
    s_mitsuyasu_bounds = ValueBounds(float(np.min(s_mitsuyasu_total)), float(np.max(s_mitsuyasu_total)))

    # Now get Donnelan-Banner Beta, which is dependent only on omega/peakOmega
    beta = donelan_banner_beta(omega_ratio_space)
    beta_bounds = ValueBounds(float(np.min(beta)), float(np.max(beta)))

    print(f"====== Discovered Hasselmann S Bounds: [{s_hasselman_bounds.min:.8f}, {s_hasselman_bounds.max:.8f}] =======")
    print(f"====== Discovered Mitsuyasu S Bounds:  [{s_mitsuyasu_bounds.min:.8f}, {s_mitsuyasu_bounds.max:.8f}] ======")
    print(f"====== Discovered DB Beta (LUT U):     [{beta_bounds.min:.8f}, {beta_bounds.max:.8f}] ======")
    print(f"====== Discovered DB Swell (LUT V):    [{s_swell_bounds.min:.8f}, {s_swell_bounds.max:.8f}] ======\n")
    return s_hasselman_bounds, s_mitsuyasu_bounds, beta_bounds, s_swell_bounds


def slang_float(value: float) -> str:
    """ A Slang float literal: 9 significant digits, always with a decimal point or exponent, so 14490 is 14490.0f """
    text = f"{value:.9g}"
    if "." not in text and "e" not in text:
        text += ".0"

    return text + "f"


# math.lgamma over an array: the standard library's log-gamma, so scipy is not a build dependency
_lgamma = np.vectorize(math.lgamma, otypes=[np.float64])


def evaluate_exact_Qs(s: np.ndarray) -> np.ndarray:
    """
    Calculates the exact normalization factor Q(s) for the directional spreading function based on the given spread
    power s. Done in log space to avoid numerical issues with large factorials. Key cost and why we don't compute
    this live is lgamma just isn't feasible on the GPU really
    """
    ln_Q = (2 * s - 1) * np.log(2) - np.log(np.pi) + 2 * _lgamma(s + 1) - _lgamma(2 * s + 1)
    return np.exp(ln_Q)


def fit_segment_loglog(s_low: float, s_high: float, degree: int) -> tuple:
    """
    Fits a polynomial to the exact Q(s) function in log-log space over the given range of s values.
    Returns the polynomial coefficients and the maximum relative error of the fit.

    The samples are spaced evenly in log(s), the space the fit lives in: spaced evenly in s, a range like
    [1e-4, 2.6e6] would put almost no samples at small s, and the reported error would say nothing about it.
    """
    NUM_POINTS: int = 1024
    S = np.geomspace(s_low, s_high, NUM_POINTS)
    X = np.log(S)
    truth = evaluate_exact_Qs(S)
    Y = np.log(truth)

    polynomial_series = np.polynomial.polynomial.Polynomial.fit(X, Y, degree)
    coeffs = polynomial_series.convert(kind=np.polynomial.polynomial.Polynomial).coef
    fitted = np.exp(np.polynomial.polynomial.polyval(X, coeffs))
    return coeffs, float(np.max(np.abs(fitted - truth) / truth))


def generate_hlsl_horner_expression(coeffs, var, use_mad=True):
    """
    Generates a string representing the evaluation of the polynomial with the given coefficients using Horner's method
    in Slang syntax. use_mad=True recommended -- faster and more accurate than separate mul+add.

    Args:
        coeffs (list): The coefficients of the polynomial, ordered from lowest degree to highest.
        var (str): The variable name to use in the expression (representing our "x" value).
        use_mad (bool): Whether to use the mad() function for fused multiply-add operations.
    Returns:
        str: A string representing the polynomial evaluation in Slang syntax.
    """
    if use_mad:
        expr = f"mad({slang_float(coeffs[-1])}, {var}, {slang_float(coeffs[-2])})"
        for c in reversed(coeffs[:-2]):
            expr = f"mad({expr}, {var}, {slang_float(c)})"
    else:
        expr = slang_float(coeffs[-1])
        for c in reversed(coeffs[:-1]):
            expr = f"({expr} * {var} + {slang_float(c)})"

    return expr


def get_normalization_shader_function_str(name: str,
                                          bounds: ValueBounds,
                                          split: float,
                                          coeffs_low: list,
                                          coeffs_high: list,
                                          error_bounds: ValueBounds) -> str:
    """
    Returns the full shader function for the normalization factor Q(s) of the given directional spread function.
    s is clamped to the fitted range first, so s = 0 (no swell, at the spectrum's edges) never reaches log().
    """
    low_expr = generate_hlsl_horner_expression(coeffs_low, 'lnS')
    high_expr = generate_hlsl_horner_expression(coeffs_high, 'lnS')

    str_buffer = StringIO()
    print(f"// {name} normalization Q(s) -- fit in ln(s)/ln(Q) space", file=str_buffer)
    print(f"// valid range: s in [{bounds.min:.6g}, {bounds.max:.6g}], clamped to it", file=str_buffer)
    print(f"// max relative error: low segment {error_bounds.min:.2e}, high segment {error_bounds.max:.2e}", file=str_buffer)
    print(f"internal float {name}NormalizationFactor(in float s)", file=str_buffer)
    print("{", file=str_buffer)
    print(f"    static const float {name}SplitS = {slang_float(split)};", file=str_buffer)
    print(f"    const float lnS = log(clamp(s, {slang_float(bounds.min)}, {slang_float(bounds.max)}));", file=str_buffer)
    print(f"    if (s <= {name}SplitS)", file=str_buffer)
    print("    {", file=str_buffer)
    print(f"        return exp({low_expr});", file=str_buffer)
    print("    }", file=str_buffer)
    print("    else", file=str_buffer)
    print("    {", file=str_buffer)
    print(f"        return exp({high_expr});", file=str_buffer)
    print("    }", file=str_buffer)
    print("}\n", file=str_buffer)
    return str_buffer.getvalue()


def find_best_split(bounds: ValueBounds, degree: int, n_candidates: int = 256) -> tuple:
    """
    Finds the split point of the two-piece polynomial fit that gives the lowest worst-case error. The candidates are
    spaced evenly in log(s), like the fit samples.

    Args:
        bounds (ValueBounds): The bounds of the s range to fit.
        degree (int): The degree of the polynomial to fit.
        n_candidates (int): The number of candidate split points to evaluate.
    Returns:
        tuple: A tuple containing best split point, worst error, and found coefficients along with error bounds
    """
    # Exclude the endpoints to avoid trivial fits
    candidate_split_values = np.geomspace(bounds.min, bounds.max, n_candidates)[1:-1]

    # "best_worst" is just the lowest of the two segment errors... confusing name but idk what else to use
    best_split, best_worst_error = None, np.inf
    best_coefficients = None

    for split_value in candidate_split_values:
        # low/high: low or high segment of the piecewise polynomial fit
        coeffs_low, error_low = fit_segment_loglog(bounds.min, split_value, degree)
        coeffs_high, error_high = fit_segment_loglog(split_value, bounds.max, degree)

        current_error = max(error_low, error_high)
        if current_error < best_worst_error:
            best_split = split_value
            best_worst_error = current_error
            best_coefficients = (coeffs_low, coeffs_high, ValueBounds(error_low, error_high))

    return best_split, best_worst_error, best_coefficients


def get_polynomial_fit_function_str(bounds: ValueBounds, label: str, max_degree: int) -> str:
    """
    Generates the polynomial fits for the given bounds out to max_degree, and returns the generated shader code.
    The lower bound is raised to S_FLOOR, since the fit is in log(s).
    """
    fit_bounds = ValueBounds(max(bounds.min, S_FLOOR), bounds.max)
    print(f"====== Fitting {label} over s in [{fit_bounds.min:.6g}, {fit_bounds.max:.6g}] (degree {max_degree}) ======")

    split, worst_err_split, (coeffs_low, coeffs_high, error_bounds) = find_best_split(fit_bounds, max_degree)
    shader_str = get_normalization_shader_function_str(label.replace(" ", ""),
                                                       fit_bounds,
                                                       split,
                                                       coeffs_low,
                                                       coeffs_high,
                                                       error_bounds)

    print(f"    Auto-split: s = {split:.6g} | Max Rel Err = {worst_err_split:.4e}")
    print(f"====== Polynomial Fit for {label} complete ======")
    return shader_str


def write_shader_snippets_to_file(file_path: Path, filename: str, snippets: list):
    """
    Writes the given shader snippets to file_path / filename, as a part of the OceanFft module. The file is only
    rewritten when its contents change, so an unchanged run does not touch the source tree or re-run the cook.
    """
    contents = StringIO()
    contents.write("// Auto-generated by assets/scripts/ocean_fft_support.py from OceanFftConfig.yaml -- do not edit\n")
    contents.write("// directional spreading normalization factors, and the Donelan-Banner LUT sampling function\n")
    contents.write("implementing OceanFft;\n")
    contents.write("__include OceanFftBindings;\n\n")
    for snippet in snippets:
        contents.write(snippet)

    write_if_changed(file_path / filename, contents.getvalue())


def write_if_changed(path: Path, text: str):
    if path.exists() and path.read_text() == text:
        print(f"=== {path} is up to date ===")
        return

    print(f"=== Writing {path} ===")
    path.write_text(text)


def swell_lobe_integral_reference(beta: float, s: float, samples: int = 400001) -> float:
    """ The Donelan-Banner integral by a dense trapezoid over [-pi, pi], to check the quadrature against """
    theta = np.linspace(-np.pi, np.pi, samples)
    integrand = (1.0 / np.cosh(beta * theta)) ** 2 * np.maximum(0.0, np.cos(theta / 2.0)) ** (2.0 * s)
    return float(np.trapezoid(integrand, theta))


def swell_from_v(v: np.ndarray, swell_bounds: ValueBounds) -> np.ndarray:
    """ The LUT's V axis: s = min + (max - min) v^2, so texels crowd at small s, where realistic swell lives """
    return swell_bounds.min + (swell_bounds.max - swell_bounds.min) * v * v


def generate_db_normalization_lut(beta_bounds: ValueBounds,
                                  swell_bounds: ValueBounds,
                                  lut_params: LUTParameters,
                                  n_nodes=48) -> tuple:
    """
    Generates a 2D LUT for the Donelan-Banner normalization factor Q = 1 / integral of sech^2(beta theta) cos(theta/2)^(2s)
    over [-pi, pi], with Gauss-Legendre quadrature.

    The quadrature window is the narrower of the two factors' widths: sech^2 decays within 8 / beta, and the swell lobe
    narrows like 1 / sqrt(s). A fixed window of pi failed at large s (73% error at s = 1000, 100% at 14490).

    Returns the bounds of Q, and the LUT as rows of swell (y), each row a run of beta (x): texture x = beta, y = swell.
    """
    print(f"====== Generating Donelan-Banner LUT data ======")
    print(f"    Legendre-Gauss quadrature nodes: {n_nodes}")
    print(f"    LUT resolution: {lut_params.resolution}x{lut_params.resolution} (Beta x Swell)")
    if lut_params.use_f16:
        print("ERROR: the Donelan-Banner LUT is always written as f32 (R32_SFLOAT): set use_f16 to false")
        raise SystemExit(1)

    resolution_arange = np.arange(lut_params.resolution) / (lut_params.resolution - 1)
    beta = beta_bounds.min + resolution_arange * (beta_bounds.max - beta_bounds.min)   # (R,)
    swell = swell_from_v(resolution_arange, swell_bounds)                              # (R,)

    nodes, weights = np.polynomial.legendre.leggauss(n_nodes)                          # (N,)
    beta_window = np.minimum(np.pi, 8.0 / beta)                                        # (R,)
    swell_window = SWELL_WINDOW_SCALE / np.sqrt(np.maximum(swell, 1.0e-12))            # (R,)
    window = np.minimum(beta_window[:, None], swell_window[None, :])                   # (R_beta, R_swell)

    theta = window[:, :, None] * nodes[None, None, :]                                  # (R, R, N)
    w = window[:, :, None] * weights[None, None, :]                                    # (R, R, N)
    directionality = (1.0 / np.cosh(beta[:, None, None] * theta)) ** 2.0
    swell_term = np.maximum(0.0, np.cos(theta / 2.0)) ** (2.0 * swell[None, :, None])
    integral = np.sum(w * directionality * swell_term, axis=-1)                        # (R_beta, R_swell)

    # check a spread of texels against the dense reference, so a regression fails the build instead of the look
    worst_error = 0.0
    for beta_index in np.linspace(0, lut_params.resolution - 1, 5).astype(int):
        for swell_index in np.linspace(0, lut_params.resolution - 1, 5).astype(int):
            reference = swell_lobe_integral_reference(beta[beta_index], swell[swell_index])
            worst_error = max(worst_error, abs(integral[beta_index, swell_index] - reference) / reference)

    print(f"    quadrature vs dense trapezoid, worst relative error over 25 texels: {worst_error:.2e}")
    if worst_error > 1.0e-3:
        print("ERROR: the Donelan-Banner quadrature is off by more than 1e-3")
        raise SystemExit(1)

    # transposed, so each row is one swell value: row-major upload makes x = beta, y = swell
    lut_data = (1.0 / integral).T.astype(np.float32)
    q_bounds = ValueBounds(float(np.min(lut_data)), float(np.max(lut_data)))
    print(f"    Q value range across the LUT: [{q_bounds.min:.6f}, {q_bounds.max:.6f}] (span {q_bounds.max / max(q_bounds.min, 1e-6):.2f}x)\n")
    print(f"====== Finished generating Donelan-Banner LUT data ======\n")
    return q_bounds, lut_data


def generate_sincos_lut(lut_params: LUTParameters) -> np.ndarray:
    """
    Generates the 1D sincos LUT for one full period [0, 2*pi). Entry i stores e^(-i theta) = (cos theta, -sin theta)
    for theta = 2*pi*i/N, packed as f16x2 into uint32: low 16 bits = cos, high 16 bits = -sin. The negated sin saves
    the conjugate in the shader; a call site that needs e^(+i theta) negates .y.

    N is the largest IFFT size, so one table serves every size: size n reads entry p * (N / n), with an integer Load.
    """
    if not lut_params.use_f16:
        print("ERROR: the sin/cos LUT is always written as f16x2 (R16G16_SFLOAT): set use_f16 to true")
        raise SystemExit(1)

    N = lut_params.resolution
    theta = (2.0 * np.pi / N) * np.arange(N, dtype=np.float64)
    cos_bits = np.cos(theta).astype(np.float16).view(np.uint16).astype(np.uint32)
    sin_bits = (-np.sin(theta)).astype(np.float16).view(np.uint16).astype(np.uint32)
    return (sin_bits << 16) | cos_bits


def write_cpp_lut(output_dir: Path, array_name: str, data: np.ndarray, width: int, height: int):
    """
    Writes the LUT as a C++ header and source pair: a uint32_t array in hex, 9 values per line, plus its width and
    height. data must be a uint32 numpy array -- the caller handles float reinterpretation.
    """
    flat = data.flatten().astype(np.uint32)
    N = len(flat)
    assert N == width * height
    VALUES_PER_LINE = 9

    header = StringIO()
    header.write("// Auto-generated by assets/scripts/ocean_fft_support.py -- do not edit\n")
    header.write("#pragma once\n")
    header.write("#include <cstdint>\n\n")
    header.write(f"constexpr uint32_t {array_name}Width = {width}u;\n")
    header.write(f"constexpr uint32_t {array_name}Height = {height}u;\n")
    header.write(f"extern const uint32_t {array_name}[{N}];\n")

    source = StringIO()
    source.write("// Auto-generated by assets/scripts/ocean_fft_support.py -- do not edit\n")
    source.write(f"#include \"{array_name}.hpp\"\n\n")
    # extern: a namespace-scope const has internal linkage otherwise, and the header's declaration would not link
    source.write(f"extern const uint32_t {array_name}[{N}] = {{\n")
    for i in range(0, N, VALUES_PER_LINE):
        chunk = flat[i: i + VALUES_PER_LINE]
        hex_vals = ", ".join(f"0x{v:08X}" for v in chunk)
        trailing = "," if (i + VALUES_PER_LINE) < N else ""
        source.write(f"    {hex_vals}{trailing}\n")

    source.write("};\n")

    write_if_changed(output_dir / f"{array_name}.hpp", header.getvalue())
    write_if_changed(output_dir / f"{array_name}.cpp", source.getvalue())


def generate_db_lut_sampling_function(beta_bounds: ValueBounds, swell_bounds: ValueBounds, resolution: int = 256) -> str:
    """
    Generates SampleDonelanBannerLUT. It reads the normTexture and normSampler globals directly: the cook records
    sampler and texture pairs by tracing them back to bound variables, which a function parameter hides. SampleLevel,
    because compute has no implicit LOD. v = sqrt((s - min) / (max - min)), the inverse of swell_from_v.
    """
    beta_range = beta_bounds.max - beta_bounds.min
    swell_range = swell_bounds.max - swell_bounds.min
    uv_scale = (resolution - 1.0) / resolution
    uv_bias = 0.5 / resolution

    lines = [
        "// Donelan-Banner normalization Q(beta, s) from the LUT: x = beta (linear), y = swell s (square root mapped)",
        "internal float SampleDonelanBannerLUT(in float beta, in float swell)",
        "{",
        f"    const float u = saturate((beta - {slang_float(beta_bounds.min)}) / {slang_float(beta_range)});",
        f"    const float v = sqrt(saturate((swell - {slang_float(swell_bounds.min)}) / {slang_float(swell_range)}));",
        f"    const float2 uv = mad(float2(u, v), {slang_float(uv_scale)}, {slang_float(uv_bias)});",
        "    return normTexture.SampleLevel(normSampler, uv, 0.0f).r;",
        "}",
        "",
    ]
    return "\n".join(lines) + "\n"


def configure_args():
    parser = argparse.ArgumentParser(description="Generate polynomial fits and LUTs for the Ocean-FFT directional spreading functions from bounded physical parameters")
    parser.add_argument("--config", type=str, required=True, help="Path to the YAML configuration file (OceanFftConfig.yaml)")
    parser.add_argument("--lut_dir", type=str, required=True, help="Directory to write the generated C++ LUT sources to")
    parser.add_argument("--shader_dir", type=str, required=True, help="Directory to write OceanFftGenerated.slang to")
    return parser


def load_config(config_path: Path) -> dict:
    """
    For organization's sake in the yaml, each parameter is in a group. Then within groups, each field is given a
    "name" and a further list of key:value params. This flattens that and strips the annotations we don't need.
    """
    with open(config_path, 'r') as f:
        config = yaml.safe_load(f)

    config_args = {}
    for group in config:
        for param in config[group]:
            name = param.get("name")
            if name is None:
                continue

            # remove readability annotations so we only keep the actual config data
            param.pop("unit", None)
            param.pop("name", None)

            if "value" in param:
                param = param.get("value")
            elif "values" in param:
                param = param.get("values")
            elif "min" in param and "max" in param:
                param = ValueBounds(min=param["min"], max=param["max"])
            elif "resolution" in param and "use_f16" in param:
                param = LUTParameters(resolution=param["resolution"], use_f16=param["use_f16"])
            elif "path" in param:
                param = param["path"]

            config_args[name] = param

    return config_args


if __name__ == "__main__":
    args = configure_args().parse_args()

    lut_dir = Path(args.lut_dir)
    shader_dir = Path(args.shader_dir)
    lut_dir.mkdir(parents=True, exist_ok=True)
    if not shader_dir.is_dir():
        print(f"ERROR: shader directory {shader_dir} does not exist")
        raise SystemExit(1)

    config_args = load_config(Path(args.config))
    if max(config_args["fft_resolution"]) > MAX_IFFT_SIZE:
        print(f"ERROR: fft_resolution lists {max(config_args['fft_resolution'])}, but the shaders run at most {MAX_IFFT_SIZE}")
        raise SystemExit(1)

    S_HasselmanBounds, S_MitsuyasuBounds, BetaBounds, S_SwellBounds = calculate_parameter_space(
        config_args["wind_speed"],
        config_args["fetch_length"],
        config_args["swell"],
        config_args["omega_ratio"].min,
        config_args["omega_ratio"].max)

    db_params = config_args["donnelan_banner_spreading"]
    q_bounds, lut_data = generate_db_normalization_lut(BetaBounds, S_SwellBounds, db_params)
    write_cpp_lut(lut_dir, "DonelanBannerNormLUT", lut_data.view(np.uint32), db_params.resolution, db_params.resolution)

    sincos_params = config_args["complex_exp_sincos"]
    if sincos_params.resolution != MAX_IFFT_SIZE:
        print(f"ERROR: complex_exp_sincos resolution must be the largest IFFT size, {MAX_IFFT_SIZE}")
        raise SystemExit(1)

    write_cpp_lut(lut_dir, "SinCosLUT", generate_sincos_lut(sincos_params), sincos_params.resolution, 1)

    mitsuyasu_str = get_polynomial_fit_function_str(S_MitsuyasuBounds, "Mitsuyasu", config_args["mitsuyasu_polynomial_degree"])
    hasselman_str = get_polynomial_fit_function_str(S_HasselmanBounds, "Hasselman", config_args["hasselmann_polynomial_degree"])
    sampling_function_str = generate_db_lut_sampling_function(BetaBounds, S_SwellBounds, db_params.resolution)
    write_shader_snippets_to_file(shader_dir, "OceanFftGenerated.slang", [mitsuyasu_str, hasselman_str, sampling_function_str])
