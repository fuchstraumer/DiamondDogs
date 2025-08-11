# DiamondDogs

WIP "toy" rendering engine, acting as a testbed and sandbox for me to implement various game engine systems that interest me, along with ideally allowing me to implement and rapidly iterate on advanced rendering techniques. Nothing about this repository is designed for shipping a game - for that, I personally would prefer to use UE5 - but it is designed to act as a space for me to build my skills, keep myself sharp, and enjoy the game engine engineering challenges that I do. Additionally, after the fun I had creating Caelestis, I'm going to be creating a similar project not using plugins (instead sticking to static library "modules"). There are lots of difficulties and disadvantages in a plugin-driven system, and it constrains the C++ features I can use unduly and in a way I find unsatisfying. I have to account for these constraints as part of my day-to-day work when I'm working for game studios - no reason to do it in my personal hobby project!

Consult the [public Trello board](https://trello.com/b/F5LTrTTT/diamonddogs) if you wish to see what I have planned so far, and for insight on what I'm working on.

I'll also be writing articles about this project for my site: eventually, there will be a tag for DiamondDogs there so you can isolate down to what content I've written specifically for this project.

## Structure

Unlike usual approaches, the "Modules" make up the base layer. The core runtime uses the modules - this way as the project evolves, the core runtime represents the uppermost layer that is acting as the fusion of all the lower level modules. As the core expands, functionality that can compartamentalized can be transferred into a module, which the core can then continue using. By composing modules, I can create more advanced functionality that should hopefully begin to approach the usual expected "edit and create gameplay in an editor" approach of things like Unity and Unreal, but for now it's mostly simple low-level functionality. It also allows me do do things like create a compartmentalized process to run things like the content compiler for offline builds and packaging, a topic I am trying to learn more about.

## Core Tech Stack

- **C++23** : I originally wrote this and much of my code for C++14, then a little bit of C++17, then got lost in employment for long enough that now C++23 is the cutting edge. I've moved to that, and plan to continue using it as many of it's features seem quite fun to play with. And as I mentioned earlier, it's rare I get to do so in commercial products...
- **Vulkan 1.4** : I am using the latest version of Vulkan as I would like to play with all the fun and niche extensions it has. Sorry if you don't have hardware that supports that!
    - In the future, I may try to add more ability to handle "downgrades" into lower versions, but for now I'm focusing on other things.
- **Windows only** : At the moment, I am only implementing this for Windows. I am familiar with multiplatform development, but don't currently have a functional Linux install to run on. This is something I aim to change in the near future, as I quite enjoy development on Ubuntu
- **Slang Shaders** : I've retired [ShaderTools](https://github.com/fuchstraumer/ShaderTools), as it was simply not full featured enough (though it was a very educational experience to make!). Instead, we are using [Slang](https://github.com/shader-slang/slang) as it makes handling large shader codebases easier, gives me more agility and capability of even supporting other APIs/RHIs eventually, and just feels plain nice to use. It even has a language server, which anyone who's worked with shader code a lot knows is super darn rare!
- **CMake Build system** : For better or worse, this repository uses CMake extensively. 
- **Mimalloc integration** : Mimalloc is integrated at the core foundation level to override and handle all memory allocations performed by executables this project creates
    - **VMA Integration** : For GPU memory, we're using VMA since it's an excellent product (thanks so much AMD, you know I love you)
- **Message Passing design** : Most modules are designed to share memory by communicating, rather than communicating by sharing memory. This may sound like a silly phrase, but the gist of it is that I want individual modules to be able to think of themselves as single threaded and not needing to worry about threading design. It also helps to force us to encapsulate functionality.
    - Erlang was something I thought about a lot here, thanks to a brilliant friend who spent a lovely car trip to Portland telling me all about it. What a neat language!
- **Optimistic concurrency design** : We avoid locking as much as possible, instead using things like the MWSR queue in foundation to act as the interface from a module to the outside world. Mutexes make me feel bad (and they inherently prevent concurrency!).


## Building / Installing

I cannot currently recommend attempting this: there's not a lot to see, but things should be fairly easy to build and use so far. All dependencies should be implemented robustly as submodules. There are integration tests you can run, as well - currently testing the rendering context (render a simple triangle) and the resource context (asynchronously load assets, create lots of resources, handle swapchain events), and hopefully soon one for the rendergraph too. You shouldn't need to copy files around: required assets are copied as needed using CMake. You might find some benefit in adjusting the `RendererContextCfg.json` for your platform, however.

## Whats the name about?

The name of this repository is from Alastair Reynold's short story anthology ["Diamond Dogs, Turquoise Days"](https://en.wikipedia.org/wiki/Diamond_Dogs,_Turquoise_Days). I liked both stories immensely, have always adored Reynold's work (and hard sci-fi in general), and as I plan to eventually have this be a tech demo related to grand dreams and technologies from science fiction it felt apt. 
