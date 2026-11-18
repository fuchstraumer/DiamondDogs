implementing PlanetLab;
import LodestoneAttributes;
/*
    This file contains the parameters used as axis parameters in the planet gen shaders,
    i.e, those that are frozen per shader variant. These drive branching behavior, or
    set the dimensions of some of our shaders. Freezing them here allows optimizing the
    shader code, and lets us create some of the backing resources this uses just be reading
    reflection data of the compiled shader variants!
*/
// 100,000 is the "legacy" option - our test verification bins of data are sized for that
[ls_axis_values("64000, 100000, 128000, 256000")][ls_axis_kind("tuning")]
extern static const int PlanetGenCellCount = 100000; // number of cells in the icosphere grid
