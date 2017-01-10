This is a set of preloadable libs for simplified OpenGL game modding.

* notsanae: the project that started this whole mess, added here for historical
  reasons.
* gltexdump: dumps any loaded textures.
* gltexmod: overrides loaded textures.
* glshaderdump (TODO): dumps any loaded glsl shaders.
* glshadermod (TODO): overrides loaded glsl shaders.
* glmeshdump (TODO): dumps any created vertex buffers.
* glmeshmod (TODO): overrides vertex buffers.

The glmod libraries need read and write access to a common folder, by default
this is ```/usr/share/glmod```, remember to create the dir with the right
permissions and ownership to get things working.

A sister project for overriding sounds in OpenAL will be made later.

All the code here is released under the GNU GPLv3.
