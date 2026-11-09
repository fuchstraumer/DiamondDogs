// The noise bases: one octave of coherent noise each. Each basis returns about [-1, 1], with a scale constant
// measured by assets/scripts/measure_noise_ranges.py, and an analytic gradient.
module NoiseBases;
import LodestoneAttributes;
import NoiseHash;
__exported import NoiseLattice;
__exported import NoiseField;

__include BasisPerlin;
__include BasisSimplex;
__include BasisValue;
