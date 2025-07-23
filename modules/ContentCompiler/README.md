# Content Compiler module

This module takes data in intermediary formats, and compiles them / transform them into the internal formats used by the engine. Primarily, it exists to translate meshes into a more universal format along with making sure they have the necessary attributes to be rendered completely. This includes generation of the tangent space, and normals if those are somehow missing. Generated data can then be retrieved, along with a header providing useful info about the attributes of the loaded mesh data.
