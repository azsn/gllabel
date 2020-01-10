all:
	g++ -Wall -g -o demo demo.cpp label.cpp -I/usr/include/freetype2 -I/usr/include/libdrm -I/usr/include/GLFW -I/usr/include/GL -lGLEW -lGLU -lGL -lglfw -lfreetype
