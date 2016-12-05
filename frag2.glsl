#version 330 core
layout(location = 0) out vec4 color;
// out vec4 color;
in vec2 oUV;
in vec3 oColor;
uniform sampler2D uSampler;
uniform vec2 t;

vec2 SineWave( vec2 p )
{
// convert Vertex position <-1,+1> to texture coordinate <0,1> and some shrinking so the effect dont overlap screen
// p.x=( 0.55*p.x)+0.5;
// p.y=(-0.55*p.y)+0.5;
// // wave distortion
float x = sin( 25.0*p.y + 30.0*p.x + 6.28*t.x) * 0.05;
float y = sin( 25.0*p.y + 30.0*p.x + 6.28*t.y) * 0.05;
return vec2(p.x+x, p.y+y);
}

void main()
{
	color = texture(uSampler, SineWave(oUV));
	
	// color = texture(uSampler, oUV + 0.005*vec2( sin(time+1536*oUV.x),cos(time+1152.0*oUV.y)) );
	// color = texture(uSampler, oUV);
	// color.a = 1;
	// color = vec4(0,0,1,1);
	// gl_FragColor = vec4(0,0,0,1);
	// gl_FragColor = vec4(1,0,0,1);
}