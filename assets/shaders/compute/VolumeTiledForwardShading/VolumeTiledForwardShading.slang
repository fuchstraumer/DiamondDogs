module VolumeTiledForwardShading;

// Configuration and shared declarations come first, because every other file reads them.
__include VtfConfig;
__include VtfTypes;
__include VtfFunctions;
__include VtfBindings;

// Compute passes, in the order the frame runs them.
__include VtfSmallEntrypoints;
__include VtfComputeClusterAABBs;
__include VtfReduceLightAABBs;
__include VtfComputeMortonCodes;
__include VtfRadixSort;
__include VtfMergeSort;
__include VtfBuildBVH;
__include VtfAssignLightsToClustersBVH;

// Raster passes.
__include VtfClusterSamples;
__include VtfDebugClusters;
__include VtfDebugLights;
__include VtfDebugTexture;
