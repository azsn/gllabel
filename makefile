all:
	g++ -o demo -I/usr/include/freetype2 -I/usr/include/libdrm -I/usr/include/GLFW -I/usr/include/GL -lGLEW -lGLU -lGL -lglfw -lfreetype demo.cpp label.cpp