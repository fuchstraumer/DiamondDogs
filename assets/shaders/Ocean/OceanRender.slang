module OceanRender;

// The ocean's render passes: the displaced surface (OceanSurface.slang), its shading (OceanShading.slang), and a
// flat seabed under it (Seabed.slang). They read the Displacement and Derivatives textures that the OceanFft compute
// module writes, one array layer per cascade.
__include OceanRenderCommon;
__include OceanSurface;
__include OceanShading;
__include Seabed;
