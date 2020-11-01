# GLLabel - GPU Vector Text Rendering

A C++ library for rendering vector text on the GPU.

**Work in progress. You probably don't want to use this yet.**

This rendering method allows text to be arbitrarily scaled and rotated, just as
it could be with CPU-rendered text. The glyphs are loaded in real-time from
font files. This project was made possible by [Will Dobbie](https://wdobbie.com)'s
algorithm for GPU rendering vector shapes and text defined by quadratic Bézier
curves. This project builds on that algorithm by implementing a framework to
load and display arbitrary text rather than the pregenerated data set used in
his demo.

## Related / relevant work

* [Valve Signed Distance Fields (2007)](https://web.archive.org/web/20180127223712/https://www.valvesoftware.com/publications/2007/SIGGRAPH2007_AlphaTestedMagnification.pdf) - Early research into GPU text rendering
* [GPU text rendering with vector textures (2016)](https://wdobbie.com/post/gpu-text-rendering-with-vector-textures/) - Will Dobbie's algorithm mentioned above
* [Easy Scalable Text Rendering on the GPU (2016)](https://medium.com/@evanwallace/easy-scalable-text-rendering-on-the-gpu-c3f4d782c5ac) - Rendering vector shapes with polygons
* [Slug](https://sluglibrary.com/) ([algorithm whitepaper (2017)](http://jcgt.org/published/0006/02/02/)) - Closed-source commercial GPU vector text renderer
* [Pathfinder](https://github.com/servo/pathfinder) ([algorithm overview (2019)](https://nical.github.io/posts/a-look-at-pathfinder.html)) - Open-source experimental GPU vector graphics renderer
* [A Primer on Bézier Curves](https://pomax.github.io/bezierinfo/) - almost everything you ever wanted to know about Bézier curvess

## Demo
![Demo video](demo.gif)

## Building

Library dependencies
* C++14
* GLEW
* GLM
* FreeType2 (used only to load outline data from font files, not for rasterization)

Additional demo dependencies
* GLFW

To build the library and demo, modify the `_INCLUDES` and `_LIBS` lines at the
top of `makefile` to match your system's configuration, then run `make`.

## License

The code in this project is licensed under the Apache License v2.0.
