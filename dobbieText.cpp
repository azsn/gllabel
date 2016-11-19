/*
 * This work is based on Will Dobbie's WebGL vector-based text rendering (2016).
 * It can be found here:
 * http://wdobbie.com/post/gpu-text-rendering-with-vector-textures/
 * Modified by Aidan Shafran, 2016.
 *
 * This code currently has no license, since Dobbie's original code had none.
 * I'll ask him for a license at some point.
 *
 * Currently, this code just renders one letter at the center of the screen
 * at different scales. Dobbie's original code used a pre-generated bezier
 * curve atlas for a known set of glyphs (glyphs.bmp).The goal is to make live
 * text rendering using TTF fonts, with bezier curve information generated
 * probably using FreeType. Also eventually I'll add back in the texture-based
 * rendering for when the font is zoomed out too much.
 *
 * The glyphfs.glsl and glyphvs.glsl shaders are almost direct copies of
 * the original WebGL shaders.
 */

#include <fstream>
#include <glew.h>
#include <vector>
#include <string>
#include <cmath>

struct bmp
{
	GLuint width, height, length;
	uint8_t *data; // rgba;
};

static GLuint glyphProgram = 0;
static bmp atlas = {0};
static bmp rawGlyphs = {0};
static GLuint atlasTexId = 0;
static GLuint glyphBuffer = 0;

static GLuint uAtlasSampler = 0;
static GLuint uTexelSize = 0;
static GLuint uDebug = 0;
static GLuint uPositionMul = 0;
static GLuint uPositionAdd = 0;
static unsigned int numGlyphs = 0;

extern GLuint loadShaderProgram(const char *vertexShaderPath, const char *fragmentShaderPath);


bool loadBMP(const char *path, bmp *s)
{
	FILE *f = fopen(path, "rb");
	if(!f)
		return false;
	
	fseek(f, 18, SEEK_SET);
	
	unsigned char bytes[4];
	
	fread(bytes, 4, 1, f);
	int width = bytes[0] | (bytes[1]<<8) | (bytes[2]<<16) | (bytes[3]<<24);
	
	fread(bytes, 4, 1, f);
	int height = bytes[0] | (bytes[1]<<8) | (bytes[2]<<16) | (bytes[3]<<24);
	
	s->width = width;
	s->height = height;
	
	fseek(f, 0, SEEK_END);
	s->length = ftell(f)-54;
	
	fseek(f, 54, SEEK_SET);
	
	s->data = new uint8_t[s->length];
	fread(s->data, s->length, 1, f);
	
	fclose(f);
	
	
	return true;
}

unsigned short ushortWithFlag(unsigned short x, unsigned short flag)
{
	return (x|0)*2 + (flag ? 1:0);
}

void initDobbie()
{
	glyphProgram = loadShaderProgram("../shaders/glyphvs.glsl", "../shaders/glyphfs.glsl");
	
	uAtlasSampler = glGetUniformLocation(glyphProgram, "uAtlasSampler");
	uTexelSize = glGetUniformLocation(glyphProgram, "uTexelSize");
	uDebug = glGetUniformLocation(glyphProgram, "uDebug");
	uPositionMul = glGetUniformLocation(glyphProgram, "uPositionMul");
	uPositionAdd = glGetUniformLocation(glyphProgram, "uPositionAdd");
	
	if(!loadBMP("../dobbie/atlas.bmp", &atlas))
	{
		printf("error loading dobbie atlas\n");
		return;
	}
	
	if(!loadBMP("../dobbie/glyphs.bmp", &rawGlyphs))
	{
		printf("error loading dobbie glyphs\n");
		return;
	}
	
	glGenTextures(1, &atlasTexId);
	glBindTexture(GL_TEXTURE_2D, atlasTexId);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, atlas.width, atlas.height, 0,
		GL_RGBA, GL_UNSIGNED_BYTE, atlas.data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	
	numGlyphs = 1000;//rawGlyphs.length / 20; // 20 bytes per glyph
	printf("Loading %i glyphs\n", numGlyphs);
	
	int16_t *uposition = (int16_t *)rawGlyphs.data;
	int16_t *position = (int16_t *)rawGlyphs.data;
	uint16_t *curvesMin = (uint16_t *)(rawGlyphs.data + 4);
	int16_t *deltaNext = (int16_t *)(rawGlyphs.data + 8);
	int16_t *deltaPrev = (int16_t *)(rawGlyphs.data + 12);
	uint16_t *color = (uint16_t *)(rawGlyphs.data + 16);
	
	uint8_t *vertexBuf = new uint8_t[numGlyphs * 6 * 12](); // 6 vertices per glyph, 12 bytes per vertex
	int16_t *oPosition = (int16_t *)(vertexBuf);
	uint16_t *oCurvesMin = (uint16_t *)(vertexBuf + 4);
	uint8_t *oColor = (uint8_t *)(vertexBuf + 8);
	
	uint32_t src = 0, dst = 0;
	for(unsigned int i=0; i<numGlyphs; i++)
	{
		if(i > 0)
		{
			position[src+0] += position[src-10+0];
			position[src+1] += position[src-10+1];
		}
		
		for(unsigned int j=0;j<6;++j)
		{
			unsigned int k = (j < 4) ? j : 6 - j;
			
			// oPosition[dst+0] = position[src+0];
			// oPosition[dst+1] = position[src+1];
			// 
			// if (k == 1) {
			// 	oPosition[dst+0] += deltaNext[src+0];
			// 	oPosition[dst+1] += deltaNext[src+1];
			// } else if (k == 2) {
			// 	oPosition[dst+0] += deltaPrev[src+0];
			// 	oPosition[dst+1] += deltaPrev[src+1];
			// } else if (k == 3) {
			// 	oPosition[dst+0] += deltaNext[src+0] + deltaPrev[src+0];
			// 	oPosition[dst+1] += deltaNext[src+1] + deltaPrev[src+1];
			// }
			// 
			// printf("pos (%i, %i)\n", oPosition[dst], oPosition[dst+1]);
			
			oCurvesMin[dst+0] = ushortWithFlag(curvesMin[10+0], k & 1);
			oCurvesMin[dst+1] = ushortWithFlag(curvesMin[10+1], k > 1);
			printf("csrc: %i, %i, cdst: %i, %i\n", curvesMin[src+0], curvesMin[src+1], oCurvesMin[dst+0], oCurvesMin[dst+1]);
			// oColor[dst+0]     = color[src+0];
			// oColor[dst+1]     = color[src+1];
	
			if (i<10) {
				//console.log(i, j, oPosition[dst+0], oPosition[dst+1], positions.x[i], positions.y[i]);
			}
	
			dst += 6;
		}
		
		src += 10;
	}
	
	oPosition[0 +0] = -2553;
	oPosition[0 +1] = -3027;
	oPosition[6 +0] = -2183;
	oPosition[6 +1] = -3027;
	oPosition[12+0] = -2553;
	oPosition[12+1] = -3359;
	oPosition[18+0] = -2183;
	oPosition[18+1] = -3359;
	oPosition[24+0] = -2553;
	oPosition[24+1] = -3359;
	oPosition[30+0] = -2183;
	oPosition[30+1] = -3027;
	oColor[0 +0] = 255; oColor[0 +1] = 0; oColor[0 +2] = 0; oColor[0 +3] = 255;
	oColor[12+0] = 255; oColor[12+1] = 0; oColor[12+2] = 0; oColor[12+3] = 255;
	oColor[24+0] = 255; oColor[24+1] = 0; oColor[24+2] = 0; oColor[24+3] = 255;
	oColor[36+0] = 255; oColor[36+1] = 0; oColor[36+2] = 0; oColor[36+3] = 255;
	oColor[48+0] = 255; oColor[48+1] = 0; oColor[48+2] = 0; oColor[48+3] = 255;
	oColor[60+0] = 255; oColor[60+1] = 0; oColor[60+2] = 0; oColor[60+3] = 255;
	
	glGenBuffers(1, &glyphBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, glyphBuffer);
	glBufferData(GL_ARRAY_BUFFER, numGlyphs * 6 * 12, vertexBuf, GL_STATIC_DRAW);
	delete [] vertexBuf;
}

double zoom = 0;

void dobbieRender()
{
	zoom += 0.01;
	
	glUseProgram(glyphProgram);
	glBindBuffer(GL_ARRAY_BUFFER, glyphBuffer);
	glEnable(GL_BLEND);

	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(0, 2, GL_SHORT, GL_TRUE, 12, (void*)0);
	glVertexAttribPointer(1, 2, GL_UNSIGNED_SHORT, GL_FALSE, 12, (void*)4);
	glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, 12, (void*)8);
	
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, atlasTexId);
	glUniform1i(uAtlasSampler, 0);
	glUniform2f(uTexelSize, 1.0/atlas.width, 1.0/atlas.height);
	glUniform1i(uDebug, 0);
	
	float aspect = (768*1.5) / (1024*1.5);
	float zoomx = std::sin(zoom) + 1.01;
	float zoomy = std::sin(zoom) + 1.01;
	zoomx /= 6.0; zoomy /= 6.0;
	float translateX = 0.429;
	float translateY = 0.596;
	
	glUniform2f(uPositionMul, aspect/zoomx, 1/zoomy);
	glUniform2f(uPositionAdd, aspect * -translateX / zoomx, -translateY / zoomy);
	
	glDrawArrays(GL_TRIANGLES, 0, numGlyphs*6);
}