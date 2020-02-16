## Reactors

In DiamondDogs, Reactors are the smallest stateful building blocks of our code. They don't expose new functionality directly to the user, but are instead used in the construction of our modules and systems. Even though higher level code also uses reactor-like paradigms, these are considered different as they expose new functionality to the new engine instead of acting to build said functionality.

These files are linked together with the foundation code: this way, even though plugins and modules might not be linked together there will still be a large amount of code that can benefit from Link-Time-Optimization techniques (highly beneficial, especially as code size grows)