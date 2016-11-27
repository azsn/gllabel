#version 330 core
uniform sampler2D uGridmapSampler;
uniform sampler2D uBeziermapSampler;
uniform vec2 uGridmapTexelSize;
uniform vec2 uBeziermapTexelSize;
uniform vec2 uPositionMul;
uniform vec2 uPositionAdd;

layout(location = 0) in vec2 aPosition; // 2 shorts = 4 bytes
layout(location = 1) in vec4 aGridRect; // 4 shorts = 8 bytes
layout(location = 2) in vec4 aColor;    // 4 bytes
layout(location = 3) in vec2 aNormCoord;// 2 bytes
layout(location = 4) in float aBezierIndex; // 1 short = 2 bytes

varying vec4 vColor;
varying vec4 vGridRect;
varying vec2 vNormCoord;
varying float vBezierIndex;

void main()
{
	vColor = aColor;
	vGridRect = aGridRect;
	vNormCoord = aNormCoord;
	vBezierIndex = aBezierIndex;
	
	vec2 pos = aPosition + 0.5;
	pos.y = 1.0 - pos.y;
	pos = pos * uPositionMul + uPositionAdd;
	gl_Position = vec4(pos, 0.0, 1.0);
}


// #define kGlyphPaddingFactor 1.4

// // Get non-normalised number in range 0-65535 from two channels of a texel
// float ushortFromVec2(vec2 v) {
// 	// v.x holds most significant bits, v.y holds least significant bits
// 	return 65280.0 * v.x + 255.0 * v.y;
// }
// 
// vec2 fetchVec2(vec2 coord) {
// 	vec2 ret;
// 	vec4 tex = texture2D(uAtlasSampler, (coord + 0.5) * uTexelSize);
// 
// 	ret.x = ushortFromVec2(tex.rg);
// 	ret.y = ushortFromVec2(tex.ba);
// 	return ret;
// }
// 
// void decodeUnsignedShortWithFlag(vec2 f, out vec2 x, out vec2 flag) {
// 	x = floor(f * 0.5);
// 	flag = mod(f, 2.0);
// }
// 
// vec2 rot90(vec2 v) {
// 	return vec2(v.y, -v.x);
// }


// decodeUnsignedShortWithFlag(aCurvesMin, vCurvesMin, vNormCoord);
// vGridMin = fetchVec2(vCurvesMin);
// vec4 sizes = texture2D(uAtlasSampler, vec2(vCurvesMin.x + 2.5, vCurvesMin.y + 0.5) * uTexelSize) * 255.0;
// vGridSize = sizes.rg;

// Adjust vNormCoord to compensate for expanded glyph bounding boxes
// bool ylarger = rasteredGridSize.x < rasteredGridSize.y;
// if (ylarger) rasteredGridSize = rasteredGridSize.yx;
// vec2 expandFactor = vec2(kGlyphPaddingFactor, (rasteredGridSize.y + rasteredGridSize.x * (kGlyphPaddingFactor - 1.0)) / rasteredGridSize.y);
// if (ylarger) expandFactor = expandFactor.yx;
// vNormCoord = (vNormCoord - 0.5) * expandFactor + 0.5;
