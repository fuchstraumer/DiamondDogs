implementing OceanFft;
__include OceanFftBindings;
__include OceanFftCommon;
__include OceanFftDims;

namespace DftTranspose
{
    static const uint TILE_DIM = 32u;
    static const uint BLOCK_ROWS = 8u;
}

[shader("compute")][numthreads(DftTranspose::TILE_DIM, DftTranspose::BLOCK_ROWS, 1)]
void DftTransposeCS(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    
}
