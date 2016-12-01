#version 330 core
uniform sampler2D uBezierAtlas;
uniform vec2 uBezierTexel;
uniform vec4 uPosScale;

layout(location = 0) in vec2 vPosition;
layout(location = 1) in vec2 vData;
layout(location = 2) in vec4 vColor;

varying vec4 oColor;
varying vec2 oBezierCoord;
varying vec2 oNormCoord;
varying vec4 oGridRect;

float ushortFromVec2(vec2 v)
{
	return (v.y * 65280.0 + v.x * 255.0);
}

vec2 vec2FromPixel(vec2 coord)
{
	vec4 pixel = texture2D(uBezierAtlas, (coord+0.5)*uBezierTexel);
	return vec2(ushortFromVec2(pixel.xy), ushortFromVec2(pixel.zw));
}

void main()
{
	oColor = vColor;
	oBezierCoord = floor(vData * 0.5);
	oNormCoord = mod(vData, 2.0);
	oGridRect = vec4(vec2FromPixel(oBezierCoord), vec2FromPixel(oBezierCoord + vec2(1,0)));
	gl_Position = vec4(vPosition*uPosScale.zw + uPosScale.xy, 0.0, 1.0);
}