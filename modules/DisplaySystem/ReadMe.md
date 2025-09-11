# DisplaySystem

This system/module creates and manages the liftime of swapchain surfaces, building on top of the platform window created by the `PlatformSystem`. It takes in data about the current display paramaters, and will adjust the surface to account for those parameters. It can also be configured to do the final tonemapping step during rendering, in order to allow us to keep that pipeline close to the data that will affect the parameters used during tonemapping.
