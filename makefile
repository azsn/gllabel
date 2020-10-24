# Modify these INCLUDES and LIBS paths to match your system configuration.
# The current values are for macOS, with the glfw, glew, glm, and freetype2
# packages all installed via Homebrew.

# OpenGL
GL_INCLUDES=
GL_LIBS=-framework OpenGL

# GLFW: For creating the demo window
GLFW_INCLUDES=-I/usr/local/include/GLFW
GLFW_LIBS=-lglfw

# GLEW: OpenGL extension loader
GLEW_INCLUDES=-I/usr/local/include/GL
GLEW_LIBS=-lGLEW

# GLM: Matrix math
GLM_INCLUDES=-I/usr/local/include

# FreeType2: For reading TrueType font files
FT2_INCLUDES=-I/usr/local/include/freetype2
FT2_LIBS=-lfreetype


CC=g++
CPPFLAGS=-Wall -Wextra -g -std=c++14 -Iinclude ${GL_INCLUDES} ${GLFW_INCLUDES} ${GLEW_INCLUDES} ${GLM_INCLUDES} ${FT2_INCLUDES}
LDLIBS=${GL_LIBS} ${GLFW_LIBS} ${GLEW_LIBS} ${FT2_LIBS}

run: demo
	./demo

demo: demo.cpp lib/gllabel.cpp lib/types.cpp lib/vgrid.cpp lib/cubic2quad.cpp lib/outline.cpp
