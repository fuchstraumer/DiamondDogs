ADD_SUBDIRECTORY(OceanFft)
# the offline cooker console; it links no RHI, and its CMake file checks that
ADD_SUBDIRECTORY(ContentCooker)
# the C++ and Lodestone port of the PlanetLab plate model
ADD_SUBDIRECTORY(PlanetGen)
