module OceanRender;

import LodestoneAttributes;
import LodestoneShims;

[ls_axis_boolean][ls_axis_kind("capability")]
public extern static const bool OCEAN_USE_WAVE_OPS = true;

[ls_axis_values("16, 32, 64, 128")][ls_axis_kind("tuning")][ls_axis_active_when("OCEAN_USE_WAVE_OPS==1")]
public extern static const uint OCEAN_WAVE_SIZE = 32u;

// The ocean's render passes: the displaced surface (OceanSurface.slang), its shading (OceanShading.slang), and a
// flat seabed under it (Seabed.slang). They read the Displacement and Derivatives textures that the OceanFft compute
// module writes, one array layer per cascade.
__include OceanRenderCommon;
__include OceanEon;
__include OceanWhitecaps;
__include OceanFoam;
__include OceanSurface;
__include OceanShading;
__include Seabed;
