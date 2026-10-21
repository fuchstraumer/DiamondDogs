import numpy as np
from numpy.polynomial import Polynomial
import matplotlib.pyplot as plt
from scipy.special import gammaln
import argparse
from dataclasses import dataclass
from io import StringIO
from pathlib import Path
import yaml

"""
TODO:
- Start layering the 2D LUT as a 3D lut, with parameter ranges and resolution being the mutation parameters, so that we can have a single
  LUT that covers the entire range of expected physical parameters, and then we choose layer to sample on based on physical params and perf settings
- Potentially integrate into Unreal as a tooling script, so that we can pass values back and forth between the engine and the LUT generation script,
  and then we can have a single source of truth for these important values and won't need to manually update them in multiple places
- Maybe consider passing polynomial coefficients in through a CBV, but would restrict us to degree 3. Does mean less reliance on generated code,
  which is always brittle and a sneaky point of failure especially when accurate math is important 
"""

@dataclass
class CascadeParameters:
    length_scale: float
    min_wavelength: float
    max_wavelength: float
    min_wave_number: float
    max_wave_number: float
    min_resolution: int

@dataclass
class ValueBounds:
    min: float
    max: float

@dataclass
class LUTParameters:
    resolution: int
    use_f16: bool

def get_cascade_params_struct_hlsl_string() -> str:
    """
    Returns a string containing the HLSL struct definition for CascadeParameters,
    which can be used in shader code.
    """
    return (
        "struct CascadeParameters {\n"
        "    float length_scale;\n"
        "    float min_wavelength;\n"
        "    float max_wavelength;\n"
        "    float min_wave_number;\n"
        "    float max_wave_number;\n"
        "    int min_resolution;\n"
        "};\n"
    )

def get_cascade_params_struct_cpp_string() -> str:
    """
    Returns a string containing the C++ struct definition for CascadeParameters,
    which can be used in C++ code. We use this to make sure our schemas between
    C++/HLSL match
    """
    return (
        "struct CascadeParameters {\n"
        "    float length_scale;\n"
        "    float min_wavelength;\n"
        "    float max_wavelength;\n"
        "    float min_wave_number;\n"
        "    float max_wave_number;\n"
        "    int min_resolution;\n"
        "};\n"
    )

def write_cascade_params_struct_to_file_cpp(file_path : Path, filename : str):
    """
    First defines the struct in C++, then writes to file a std::array<CascadeParameters, num_cascades>
    with the given cascade parameters. This is used to pass the cascade parameters to the shader code.
    """

def get_cascade_wavelength_bounds(cascade : int, num_cascades : int, wavelength_bounds : ValueBounds) -> ValueBounds:
    """
    Gets the bounds of the wavelengths represented in `cascade`
    Args:
        cascade (int): The index of the cascade (0-indexed)
        num_cascades (int): The total number of cascades
        wavelength_bounds (ValueBounds): The min and max wavelengths for the entire simulation
    Returns:
        ValueBounds: An object containing the max (longest) wavelength and min (shortest) wavelength for the given cascade
    """
    wavelength_ratio = wavelength_bounds.min / wavelength_bounds.max
    wavelength_long = wavelength_bounds.max * (wavelength_ratio ** (cascade / num_cascades))
    wavelength_short = wavelength_bounds.max * (wavelength_ratio ** ((cascade + 1) / num_cascades))
    return ValueBounds(wavelength_long, wavelength_short)

def calculate_cascade_parameters(cascade : int,
                                 max_num_cascades : int,
                                 min_wave_number_per_cascade : int,
                                 wavelength_bounds : ValueBounds,
                                 margin_factor_bounds : ValueBounds) -> CascadeParameters:
    """
    Calculates the cascade parameters for each wave cascade: length scale, min/max wavelengths
    min/max wave numbers, and min resolution required to resolve the smallest waves in the cascade
    """
    wavelength_bounds = get_cascade_wavelength_bounds(cascade, max_num_cascades, wavelength_bounds)
    wave_rho = wavelength_bounds.max / wavelength_bounds.min
    # The length scale is set so we can fit min_wave_number_per_cascade waves of the longest wavelength
    # into the cascade - higher numbers mean less visible tiling, ideally
    length_scale = min_wave_number_per_cascade * wavelength_bounds.max
    # Wavelength and wave number are inversely related, so max wave number (highest spatial freq)
    # is related to the shortest wavelength, and vice versa. it still tangles my head a bit
    min_wave_number = (2 * np.pi) / wavelength_bounds.max

    # We use min margin factor for the last cascade (lowest detail), and max for inner (higher detail) cascades
    # This slightly increases the max size of the waves in the outer cascade
    small_wave_multiplier = margin_factor_bounds.min if cascade < max_num_cascades - 1 else margin_factor_bounds.max
    max_wave_number = (2 * np.pi) / wavelength_bounds.min
    # Now we can solve for the minres required - mostly used to verify our selected parameters fit
    # for the given resolution and related parameters
    min_resolution = int(np.ceil(min_wave_number_per_cascade * small_wave_multiplier * wave_rho))

    return CascadeParameters(length_scale=length_scale,
                             min_wavelength=wavelength_bounds.min,
                             max_wavelength=wavelength_bounds.max,
                             min_wave_number=min_wave_number,
                             max_wave_number=max_wave_number,
                             min_resolution=min_resolution)

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
    # Don't forget to convert fetch from km to m for the parameter space
    fetch_space = np.linspace(fetch_length_bounds.min, fetch_length_bounds.max, axis_resolution)
    swell_space = np.linspace(swell_bounds.min, swell_bounds.max, axis_resolution)
    # The important factor shaping the spreading is the ratio of omega to peakOmega, so thankfully we can just evaluate the ratio space directly
    omega_ratio_space = np.linspace(ratio_min, ratio_max, axis_resolution)

    wind_speed_grid, fetch_grid, swell_grid, omega_ratio_grid = np.meshgrid(wind_speed_space, fetch_space, swell_space, omega_ratio_space, indexing='ij')

    # Calculate the peak frequency (peakOmega) for JONSWAP based on inputs
    # chi = dimensionless fetch
    g = 9.81
    # important note: fetch_grid is in km, so we multiply by 1000 to convert to m for the dimensionless fetch calculation!
    chi = (g * fetch_grid * 1000.0) / (wind_speed_grid ** 2)
    nu_peak = 3.5 * (chi ** -0.33)
    peakOmega = 2 * np.pi * nu_peak * g / wind_speed_grid

    # Swell s - additive factor to the spreading function based on swell
    s_swell = 16.1 * np.tanh(1.0 / omega_ratio_grid) * (swell_grid**2)
    s_swell_bounds = ValueBounds(max(0.0, np.min(s_swell)), np.max(s_swell))

    # Hasselman s - the spreading factor based on the Hasselman model
    mu = -2.33 - 1.45 * ((wind_speed_grid * peakOmega / g) - 1.17)
    s_hasselman = np.where(omega_ratio_grid <= 1.0, 6.97 * (omega_ratio_grid ** 4.06), 9.77 * (omega_ratio_grid ** mu))
    s_hasselman_total = s_hasselman + s_swell
    s_hasselman_bounds = ValueBounds(np.min(s_hasselman_total), np.max(s_hasselman_total))

    # Mitsuyasu s - the spreading factor based on the Mitsuyasu model
    s_mitsuyasu_base = 11.5 * ((peakOmega * wind_speed_grid / g) ** -2.5)
    s_mitsuyasu = np.where(omega_ratio_grid <= 1.0,
                           s_mitsuyasu_base * (omega_ratio_grid ** 5.0),
                           s_mitsuyasu_base * (omega_ratio_grid ** -2.5))
    s_mitsuyasu_total = s_mitsuyasu + s_swell
    s_mitsuyasu_bounds = ValueBounds(np.min(s_mitsuyasu_total), np.max(s_mitsuyasu_total))

    # Now get Donnelan-Banner Beta, which is dependent only on omega/peakOmega
    beta = np.zeros_like(omega_ratio_space)
    for i,x in enumerate(omega_ratio_space):
        if x < 0.95:
            beta[i] = 2.61 * (x ** 1.3)
        elif x < 1.6:
            beta[i] = 2.28 * (x ** -1.3)
        else:
            p = -0.4 + 0.8393 * np.exp(-0.567 * np.log(x * x))
            beta[i] = 10 ** p

    beta_bounds = ValueBounds(np.min(beta), np.max(beta))

    print(f"====== Discovered Hasselmann S Bounds: [{s_hasselman_bounds.min:.8f}, {s_hasselman_bounds.max:.8f}] =======")
    print(f"====== Discovered Mitsuyasu S Bounds:  [{s_mitsuyasu_bounds.min:.8f}, {s_mitsuyasu_bounds.max:.8f}] ======")
    print(f"====== Discovered DB Beta (LUT U):     [{beta_bounds.min:.8f}, {beta_bounds.max:.8f}] ======")
    print(f"====== Discovered DB Swell (LUT V):    [{s_swell_bounds.min:.8f}, {s_swell_bounds.max:.8f}] ======\n")
    return s_hasselman_bounds, s_mitsuyasu_bounds, beta_bounds, s_swell_bounds

def evaluate_exact_Qs(s : np.ndarray) -> np.ndarray:
    """
    Calculates the exact normalization factor Q(s) for the directional spreading function based on the given spread
    power s. Done in log space to avoid numerical issues with large factorials. Key cost and why we don't compute
    this live is gammaln just isn't feasible on the GPU really
    """
    ln_Q = (2 * s - 1) * np.log(2) - np.log(np.pi) + 2 * gammaln(s+1) - gammaln(2 * s + 1)
    return np.exp(ln_Q)

def fit_segment_loglog(s_low: float, s_high: float, degree: int) -> tuple:
    """
    Fits a polynomial to the exact Q(s) function in log-log space over the given range of s values.
    Returns the polynomial coefficients and the maximum relative error of the fit.

    Args:
        s_low (float): The lower bound of the s range.
        s_high (float): The upper bound of the s range.
        degree (int): The degree of the polynomial to fit.

    Returns:
        tuple: A tuple containing the polynomial coefficients and the maximum relative error of the fit.
    """
    NP_LINSPACE_NUM_POINTS : int = 1024
    S = np.linspace(s_low, s_high, NP_LINSPACE_NUM_POINTS)
    X = np.log(S)
    Y = np.log(evaluate_exact_Qs(S))
    polynomial_series = np.polynomial.polynomial.Polynomial.fit(X, Y, degree)
    coeffs = polynomial_series.convert(kind=np.polynomial.polynomial.Polynomial).coef
    fitted = np.exp(np.polynomial.polynomial.polyval(X, coeffs))
    truth = evaluate_exact_Qs(S)
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
        expr = f"mad({coeffs[-1]:.9g}f, {var}, {coeffs[-2]:.9g}f)"
        for c in reversed(coeffs[:-2]):
            expr = f"mad({expr}, {var}, {c:.9g}f)"
    else:
        expr = f"{coeffs[-1]:.9g}f"
        for c in reversed(coeffs[:-1]):
            expr = f"({expr} * {var} + {c:.9g}f)"
    return expr

def get_normalization_shader_function_str(
        name : str,
        bounds : ValueBounds,
        split : float,
        coeffs_low : list,
        coeffs_high : list,
        error_bounds : ValueBounds) -> str:
    """
    Prints the full shader function for the normalization factor Q(s) for the given directional spread function

    Args:
        name (str): The name of the directional spread function
        bounds (ValueBounds): The bounds of the s range to fit.
        split (float): The split point for the piecewise polynomial fit.
        coeffs_low (list): The coefficients of the polynomial for the lower segment.
        coeffs_high (list): The coefficients of the polynomial for the higher segment.
        error_bounds (ValueBounds): The maximum relative error of the fit for each segment.
    Returns:
        str: A string containing the full shader function for the normalization factor Q(s).
    """
    low_expr = generate_hlsl_horner_expression(coeffs_low, 'lnS')
    high_expr = generate_hlsl_horner_expression(coeffs_high, 'lnS')
    # okay, no more dumping to stdout: gather the shader code into a full string, and return that
    str_buffer = StringIO()
    print(f"// {name} normalization Q(s) -- fit in ln(s)/ln(Q) space", file=str_buffer)
    print(f"// valid range: s in [{bounds.min:.4f}, {bounds.max:.4f}]", file=str_buffer)
    print(f"// max relative error: low segment {error_bounds.min:.2e}, high segment {error_bounds.max:.2e}", file=str_buffer)
    print(f"internal float {name}NormalizationFactor(in float s)", file=str_buffer)
    print("{", file=str_buffer)
    print(f"    static const float {name}SplitS = {split:.6f}f;", file=str_buffer)
    print("    const float lnS = log(s);", file=str_buffer)
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


def find_best_split(bounds : ValueBounds, degree : int, n_candidates : int = 256) -> tuple:
    """
    Previously split point of piecewise polynomials used to be based on user input,
    but now we can find the best split point automatically by evaluating the error 
    of polynomial fits. We do it in log-log space as it flattens the landscape of the functions
    and helps us converge on a good split point.

    Args:
        bounds (ValueBounds): The bounds of the s range to fit.
        degree (int): The degree of the polynomial to fit.
        n_candidates (int): The number of candidate split points to evaluate.
    Returns:
        tuple: A tuple containing best split point, worst error, and found coefficients along with error bounds
    """ 

    # Exclude the endpoints to avoid trivial fits
    candidate_split_values = np.linspace(bounds.min, bounds.max, n_candidates)[1:-1]

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

def get_polynomial_fit_function_str(bounds : ValueBounds, label : str, max_degree : int): 
    """
    Generate the polynomial fits for the given bounds and split value, out to max_degree - and writes
    the resulting generated shader code to a string that is returned (we write to file in a batch, later)
    
    Args:
        bounds (ValueBounds): The bounds of the s range to fit.
        label (str): A label for the polynomial fit (e.g., "Hasselman S" or "Mitsuyasu S")
        max_degree (int): The maximum degree of the polynomial fit
    Returns:
        str: A string containing the shader code for the polynomial fit.
    """
    print(f"====== Fitting {label} over s in [{bounds.min:.4f}, {bounds.max:.4f}] (degree {max_degree}) ======")

    split, worst_err_split, (coeffs_low, coeffs_high, error_bounds) = find_best_split(bounds, max_degree)

    shader_str = get_normalization_shader_function_str(label.replace(" ", ""),
                                                       bounds,
                                                       split,
                                                       coeffs_low,
                                                       coeffs_high,
                                                       error_bounds)
    
    print(f"    Auto-split: s = {split:.4f} | Max Rel Err = {worst_err_split:.4e}")
    print(f"====== Polynomial Fit for {label} complete ======")

    return shader_str

def write_shader_snippets_to_file(file_path : Path, filename : str, snippets : list):
    """
    Writes the given shader snippets to a file at the specified path and filename.

    Args:
        file_path (Path): The directory path where the file will be written.
        filename (str): The name of the file to write.
        snippets (list): A list of shader code snippets to write to the file.
    """
    if not file_path.exists():
        print(f"ERROR: Directory {file_path} for generated shader snippets does not exist. Exiting.")
        raise SystemExit(1)

    shader_file_path = file_path / filename
    print(f"=== Writing shader snippets to {shader_file_path} ===")
    with open(shader_file_path, 'w') as f:
        f.write("// Auto-generated Slang code -- directional spreading normalization factors\n")
        f.write("// Generated by OceanFftSupport.py\n\n")
        f.write("#pragma once\n\n")
        for snippet in snippets:
            f.write(snippet)
        # write eof?

def normalization_integrand(theta : float, beta : float, s : float) -> float:
    """ Computes normalization integrand for Donnelan-Banner spreading function, given theta, beta, and s. """
    directionality = (1.0 / np.cosh(beta * theta)) ** 2.0
    swell = (np.maximum(0.0, np.cos(theta / 2.0))) ** (2.0 * s)
    return directionality * swell


def generate_db_normalization_lut(beta_bounds : ValueBounds,
                                  swell_bounds : ValueBounds,
                                  lut_params : LUTParameters,
                                  n_nodes=48) -> tuple:

    """
    Generates a 2D LUT for the Donnelan-Banner normalization factor Q(s) based on the given bounds and LUT parameters.
    Uses Legendre-Gauss quadrature to compute the integral for each combination of beta and swell values.
    Args:
        beta_bounds (ValueBounds): The bounds of the beta range for the LUT.
        swell_bounds (ValueBounds): The bounds of the swell range for the LUT.
        lut_params (LUTParameters): The parameters for the LUT, including resolution and precision.
        n_nodes (int): The number of nodes for Legendre-Gauss quadrature (default is 48).
    Returns:
        tuple: A tuple containing the ValueBounds of the Q values and the generated LUT data as a 2D numpy array.
    """
    print(f"====== Generating Donnelan-Banner LUT data ======")
    print(f"    Legendre-Gauss quadrature nodes: {n_nodes}")
    print(f"    LUT resolution: {lut_params.resolution}x{lut_params.resolution} (Beta x Swell)")

    resolution_arange = np.arange(lut_params.resolution) / (lut_params.resolution - 1)
    beta = beta_bounds.min + resolution_arange * (beta_bounds.max - beta_bounds.min)   # (R,)
    swell = swell_bounds.min + resolution_arange * (swell_bounds.max - swell_bounds.min)  # (R,)

    nodes, weights = np.polynomial.legendre.leggauss(n_nodes)          # (N,)
    window = np.minimum(np.pi, 8.0 / beta)                             # (R,) -- per-beta half-width
    theta = window[:, None] * nodes[None, :]                           # (R, N)
    w = window[:, None] * weights[None, :]                             # (R, N)

    # broadcast: (R_beta, 1, N) vs (1, R_swell, N) via a middle axis for swell
    directionality = (1.0 / np.cosh(beta[:, None, None] * theta[:, None, :])) ** 2.0   # (R,1,N)
    cos_term = np.maximum(0.0, np.cos(theta[:, None, :] / 2.0))                        # (R,1,N)
    swell_term = cos_term ** (2.0 * swell[None, :, None])                              # (R,R,N)

    integral = np.sum(w[:, None, :] * directionality * swell_term, axis=-1)  # (R_beta, R_swell)
    lut_data = np.where(integral != 0, 1.0 / integral, 0.0).astype(np.float32)
    # note: rows=beta(U axis), cols=swell(V axis) here -- transpose if your y/x convention needs swapping
    q_bounds = ValueBounds(float(np.min(lut_data)), float(np.max(lut_data)))
    print(f"    Q value range across the LUT: [{q_bounds.min:.6f}, {q_bounds.max:.6f}] (span {q_bounds.max/max(q_bounds.min, 1e-6):.2f}x)\n")
    print(f"====== Finished generating Donelan-Banner LUT data ======\n")
    # U axis = Beta, V axis = Swell (and result = 1/integral, aka normalization factor Q(s))
    return q_bounds, lut_data

def generate_sincos_lut(lut_params : LUTParameters) -> np.ndarray:
    """
    Generate 1D sincos LUT for one full period [0, 2*pi). Entry i stores
    (cos(2*pi*i/N), sin(2*pi*i/N)) packed as f16x2 into uint32.
    Low 16 bits = cos, high 16 bits = sin -- matches ComplexExp layout (result.x=cos, result.y=sin).
    """
    N = lut_params.resolution
    theta = (2.0 * np.pi / N) * np.arange(N, dtype=np.float64)
    cos_bits = np.cos(theta).astype(np.float16).view(np.uint16).astype(np.uint32)
    sin_values = np.sin(theta).astype(np.float16)
    sin_values = -sin_values  # negate sin value, as otherwise we immediately do the conjugate in the shader, and we want to avoid that extra instruction
    sin_bits = sin_values.view(np.uint16).astype(np.uint32)
    return (sin_bits << 16) | cos_bits

def write_cpp_lut(output_dir : Path, array_name : str, data : np.ndarray):
    """
    Write constexpr uint32_t array to a C++ source/header pair.
    data must be a uint32 numpy array -- caller handles float reinterpretation.
    Values written in hex, 5 per line, fits in 120-char columns.
    """
    flat = data.flatten().astype(np.uint32)
    N = len(flat)
    VALUES_PER_LINE = 9

    header_path = output_dir / f"{array_name}.hpp"
    source_path = output_dir / f"{array_name}.cpp"

    print(f"=== Writing {array_name} -> {header_path} ===")
    with open(header_path, 'w') as f:
        f.write("// Auto-generated LUT data -- do not edit\n")
        f.write("// Generated by OceanFftSupport.py\n")
        f.write("#pragma once\n")
        f.write("#include <cstdint>\n\n")
        f.write(f"extern const uint32_t {array_name}[{N}];\n")

    print(f"=== Writing {array_name} -> {source_path} ===")
    with open(source_path, 'w') as f:
        f.write("// Auto-generated LUT data -- do not edit\n")
        f.write("// Generated by OceanFftSupport.py\n")
        f.write(f"#include \"{array_name}.hpp\"\n\n")
        f.write(f"constexpr uint32_t {array_name}[{N}] = {{\n")
        for i in range(0, N, VALUES_PER_LINE):
            chunk = flat[i : i + VALUES_PER_LINE]
            hex_vals = ", ".join(f"0x{v:08X}" for v in chunk)
            trailing = "," if (i + VALUES_PER_LINE) < N else ""
            f.write(f"    {hex_vals}{trailing}\n")
        f.write("};\n")

    print(f"=== Done: {array_name} ({N} uint32 values) ===\n")

def generate_hasselman_lut_sampling_function(beta_bounds : ValueBounds, swell_bounds : ValueBounds):
    """
    Generates a function that samples the Hasselman spreading function LUT based on the given parameters.
    Clamps input parameters to established min/max here, and then normalizes them to 0-1
    """
    function_definition_str = "internal float SampleHasselmanLUT(in float beta, in float swell, in Texture2D normTexture, in SamplerState normSampler)"
    
    beta_clamp_str = f"beta = clamp(beta, {beta_bounds.min}, {beta_bounds.max});\n"
    swell_clamp_str = f"swell = clamp(swell, {swell_bounds.min}, {swell_bounds.max});\n"

    beta_norm_divisor = beta_bounds.max - beta_bounds.min
    norm_u_str = f"float u = (beta - {beta_bounds.min}) / {beta_norm_divisor};\n"
    
    swell_norm_divisor = swell_bounds.max - swell_bounds.min
    norm_v_str = f"float v = (swell - {swell_bounds.min}) / {swell_norm_divisor};\n"
    if swell_bounds.min == 0.0:
        norm_v_str = f"float v = swell / {swell_bounds.max};\n"
    
    texture_sample_str = "return normTexture.Sample(normSampler, float2(u, v)).r;\n"
    
    # assemble function string... probably a cleaner way to do this lol
    function_str = f"{function_definition_str}\n{{\n    {beta_clamp_str}    {swell_clamp_str}    {norm_u_str}    {norm_v_str}    {texture_sample_str}}}\n"
    return function_str

def configure_args():
    # reduced to just the config file argument, as it was getting wayyyy to complex to handle on command line alone
    parser = argparse.ArgumentParser(description="Generate polynomial fits and a LUT for Ocean-FFT directional spreading functions based on bounded physical parameters")
    parser.add_argument("--config", type=str, default="OceanFftConfig.yaml", help="Path to a YAML configuration file containing the parameters for LUT generation")
    parser.add_argument("--project_root", type=str, default=".", help="Root directory of the project, used to resolve relative paths in the config file")
    parser.add_argument("--lut_dir", type=str, default="generated", help="Directory to write generated LUTs")
    parser.add_argument("--skip_shader", action="store_true", help="Skip generating shader snippets")
    parser.add_argument("--shader_dir", type=str, default="generated", help="Directory to write generated shader snippets")
    return parser

def verify_dir_or_exit(dir_path: Path, description: str):
    """
    Verifies that the given directory path exists. If it does not exist, attempts to create it.
    If creation fails, exits the program with an error message.

    Args:
        dir_path (Path): The directory path to verify.
        description (str): A description of the directory for error messages.
    """
    if not dir_path.exists():
        print(f"Directory {dir_path} for {description} does not exist. Creating it.")
        dir_path.mkdir(parents=True, exist_ok=True)
        if not dir_path.exists():
            print(f"ERROR: Directory {dir_path} for {description} does not exist and could not be created. Exiting.")
            raise SystemExit(1)

if __name__ == "__main__":
    parser = configure_args()
    args = parser.parse_args()

    project_root = Path(args.project_root)
    lut_dir = project_root / Path(args.lut_dir)
    shader_dir = project_root / Path(args.shader_dir)

    verify_dir_or_exit(project_root, "project root")
    verify_dir_or_exit(lut_dir, "generated LUTs")
    verify_dir_or_exit(shader_dir, "generated shader snippets")

    config_args = {}
    if args.config is not None:
        with open(args.config, 'r') as f:
            config = yaml.safe_load(f)
        # For organization's sake in the yaml, each parameter is in a group. Then within groups,
        # each field is given a "name" and a further list of key:value params. So, we'll take the
        # time now to flatten that and strip annotations we don't need to keep
        for group in config:
            for param in config[group]:
                name = param.get("name")
                if name is not None:

                    # remove readability annotations so we only keep the actual config data
                    if "unit" in param:
                        param.pop("unit", None)

                    if "name" in param:
                        param.pop("name", None)

                    if "value" in param:
                        # short-circuit: if there's a "value" field, just use that as the value for this parameter
                        param = param.get("value")
                    elif "values" in param:
                        # if multiple values, short-circuit to a list
                        param = param.get("values")
                    # if min/max, create ParameterBounds dataclass for it
                    elif "min" in param and "max" in param:
                        param = ValueBounds(min=param["min"], max=param["max"])
                    # if resolution and use_f16, create LUTParameters dataclass for it
                    elif "resolution" in param and "use_f16" in param:
                        param = LUTParameters(resolution=param["resolution"], use_f16=param["use_f16"])
                    elif "path" in param:
                        # if path, just use the string value. we'll convert to Path object later
                        param = param["path"]
                    
                    config_args[name] = param

    S_HasselmanBounds, S_MitsuyasuBounds, BetaBounds, S_SwellBounds = calculate_parameter_space(
        config_args["wind_speed"],
        config_args["fetch_length"],
        config_args["swell"],
        config_args["omega_ratio"].min,
        config_args["omega_ratio"].max)

    q_bounds, lut_data = generate_db_normalization_lut(BetaBounds, S_SwellBounds, config_args["donnelan_banner_spreading"])
    write_cpp_lut(lut_dir, "DonelanBannerNormLUT", lut_data.flatten().view(np.uint32))

    sincos_data = generate_sincos_lut(config_args["complex_exp_sincos"])
    write_cpp_lut(lut_dir, "SinCosLUT", sincos_data)

    mitsuyasu_str = get_polynomial_fit_function_str(S_MitsuyasuBounds, "Mitsuyasu", config_args["mitsuyasu_polynomial_degree"])
    hasselman_str = get_polynomial_fit_function_str(S_HasselmanBounds, "Hasselman", config_args["hasselmann_polynomial_degree"])
    sampling_function_str = generate_hasselman_lut_sampling_function(BetaBounds, S_SwellBounds)
    write_shader_snippets_to_file(shader_dir, "OceanFFT_Generated.slang", [mitsuyasu_str, hasselman_str, sampling_function_str])

