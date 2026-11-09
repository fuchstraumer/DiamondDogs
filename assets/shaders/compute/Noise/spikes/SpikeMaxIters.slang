// Spike S5: an octave loop whose count is a runtime uniform, under [MaxIters(16)]. The question: does it cook and
// validate on both targets, and does the SPIR-V carry the bound (LoopControl MaxIterations)?
module SpikeMaxIters;

struct SpikeOctaves
{
    int octaveCount;
    float lacunarity;
    float gain;
}

ParameterBlock<SpikeOctaves> gOctaves;
RWStructuredBuffer<float> gOctaveOut;

static const int k_MaxOctaves = 16;

[shader("compute")]
[numthreads(64, 1, 1)]
void SpikeMaxItersCS(uint3 id : SV_DispatchThreadID)
{
    const int octaveCount = min(gOctaves.octaveCount, k_MaxOctaves);
    float frequency = 1.0;
    float amplitude = 1.0;
    float sum = 0.0;

    [MaxIters(16)]
    for (int octave = 0; octave < octaveCount; ++octave)
    {
        sum += amplitude * sin(float(id.x) * frequency);
        frequency *= gOctaves.lacunarity;
        amplitude *= gOctaves.gain;
    }

    gOctaveOut[id.x] = sum;
}
