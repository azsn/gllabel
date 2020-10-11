# GLLabel - GPU Vector Text Rendering

A C++ class for rendering vector text in OpenGL.

This rendering method allows text to be arbitrarily scaled and rotated,
just as it could be with CPU-rendered text. This method does not use
Valve's Signed Distance Fields, and can load text quickly from TTF font
files in real-time.

**Work in progress.**

## Thanks

This project was made possible by [work from Will Dobbie](https://wdobbie.com)
who designed the algorithm used by the GLSL shaders to render the text. This
project builds off his work by implementing a framework around that algorithm
to load and display arbitrary text rather than the pregenerated data set used
in his demo.

## Demo
![Demo video](demo.gif)

## Building

Dependencies
* C++14
* OpenGL
* GLFW
* GLEW
* GLM
* FreeType2

To build, modify the `_INCLUDES` and `_LIBS` lines at the top of `makefile` to
match your system's configuration, then run `make`.

## License

The code in this project is licensed under the Apache License v2.0.
