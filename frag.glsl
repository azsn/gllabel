#version 330 core
layout(location = 0) out vec4 color;
// out vec4 color;
in vec2 oUV;
in vec3 oColor;
uniform sampler2D uSampler;
uniform vec2 t;

void main()
{
	color = texture(uSampler, oUV + 0.005*vec2( sin(t.x+1536*oUV.x),cos(t.y+1152.0*oUV.y)) );
	
	// gl_FragColor = vec4(0,0,0,1);
	// gl_FragColor = vec4(1,0,0,1);
}