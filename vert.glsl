#version 330 core
layout(location = 0) in vec3 vCoord;
layout(location = 1) in vec3 vColor;
layout(location = 2) in vec2 vUV;

out vec2 oUV;
out vec3 oColor;
void main()
{
	gl_Position = vec4(vCoord, 1);
	oColor = vColor;
	oUV = vUV;
}