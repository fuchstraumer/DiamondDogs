# DiamondDogs

WIP as hell (as is the usual for me) rendering/engine project. After the fun I had creating Caelestis, I'm going to be creating a similar project not using plugins (instead sticking to static library "modules"). There are lots of difficulties and disadvantages in a plugin-driven system, and I want to compare the dev time required to reach a similar point using a more coventional method. Additionally, not using plugins will allow me to use the knowledge of template metaprogramming and modern C++ features that I am increasingly acquiring. Things like coroutines, concepts, and standardized networking plus other recent or upcoming additions to the standard are too exciting for me to let go - so instead of complying to a C interface from the get-go, I'll be using whatever the most modern standard of C++ is at any given time.

This project will probably have a timeline of years. I'm a patient man, and I have a long-term vision of what I want (*almost* Shores of Hazeron esque, but open source and with a creator open to criticism ;) ) but I'm in no rush to complete it. Consult the [public Trello board](https://trello.com/b/F5LTrTTT/diamonddogs) if you wish to see what I have planned so far, and for insight on what I'm working on.

I'll also be writing articles about this project for my site: eventually, there will be a tag for DiamondDogs there so you can isolate down to what content I've written specifically for this project.

## Structure

Unlike usual approaches, the "Modules" make up the base layer. The core runtime uses the modules - this way as the project evolves, the core runtime represents the uppermost layer that is acting as the fusion of all the lower level modules. As the core expands, functionality that can compartamentalized can be transferred into a module, which the core can then continue using. 

## Building / Installing

I cannot currently recommend attempting this: there's not a lot to see, but things should be fairly easy to build and use so far. All dependencies should be implemented robustly as submodules. There are integration tests you can run, as well - currently testing the rendering context (render a simple triangle) and the resource context (asynchronously load assets, create lots of resources, handle swapchain events), and hopefully soon one for the rendergraph too. You shouldn't need to copy files around: required assets are copied as needed using CMake. You might find some benefit in adjusting the `RendererContextCfg.json` for your platform, however.

## But what about plugins, though?

So the plugin system for Caelestis proved to be REALLY neat. And had some huge advantages! But for a large project that plans to use lots of modern C++ features, it didn't make sense. It also removed the option of link-time optimization, as that's simply not possible with dynamic libraries: and it can be a really powerful source of optimization for release versions. (or has proven so, in past applications projects I've worked on).

Instead, I plan on implementing a plugin system that allows for extension of core functionality *after* completion of all the core functionality I want to get implemented. This includes most of the tasks I have logged so far on Trello, at least out to core rendering and gameplay systems. Beyond that, we'll see how it goes.

## Whats the name about?

The name of this repository is from Alastair Reynold's short story anthology ["Diamond Dogs,Turquoise Days"](https://en.wikipedia.org/wiki/Diamond_Dogs,_Turquoise_Days). I liked both stories immensely, have always adored Reynold's work (and hard sci-fi in general), and as I plan to eventually have this be a sci-fi space-related game it seemed apt. Hopefully the name will come more to light (and relevance!) as the project progresses.
