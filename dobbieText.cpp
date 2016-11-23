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
#include <math.h>
#include <glm/glm.hpp>
#include <chrono>
#include <set>
#include <ft2build.h>
#include FT_FREETYPE_H

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
static GLuint atlasBuffer = 0;

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
	return x*2 + (flag ? 1:0);
}

void lineIntersectY(glm::vec2 A, glm::vec2 B, float Y, float *X)
{
	float t = (Y - A.y) / B.y;
	*X = A.x + B.x*t;
}

/*
 * Taking two endpoints A and C, a control point B, and a horizontal line
 * y=Y, find the x values of intersection between the line and the curve.
 * This can also be used to find the y values of intersection if the x and y
 * coordinates of each point are swapped and x=Y. Returns NAN for "no X value."
 * If both X1 and X2 are NAN, the curve does not cross y=Y.
 */
void bezierIntersectY(glm::vec2 A, glm::vec2 B, glm::vec2 C, float Y, float *X1, float *X2)
{
	/*
	 * Quadratic bezier curves (with two endpoints and one control point)
	 * are represented by the function
	 * F(t) = (1-t)^2*A + 2*t*(1-t)*B + t^2*C
	 * where F is a vector function, A and C are the endpoint vectors, C is
	 * the control point vector, and 0 <= t <= 1.
	 *
	 * Solving the bezier function for t gives:
	 * t = (A - B [+-] sqrt(y*a + B^2 - A*C))/a
	 * where a = A - 2B + C.
	 * http://www.wolframalpha.com/input/?i=y+%3D+(1-t)%5E2a+%2B+2t(1-t)*b+%2B+t%5E2*c+solve+for+t
	 
	 * bezierIntersectY finds the intersection of a quadratic bezier curve with
	 * a horizontal line (y=Y). Since the y value is known, it can be used
	 * to solve for t, the time of intersection. t then can be used to solve
	 * for x.
	 */
	
	// Parts of the bezier function solved for t
	float a = A.y - 2*B.y + C.y;

	// In the condition that a=0, the standard formulas won't work
	if(a == 0)
	{
		float t = (2*B.y - C.y - Y) / (2*(B.y-C.y));
		*X1 = (t > 1 || t < 0 || isnan(t)) ? NAN : (1-t)*(1-t)*A.x + 2*t*(1-t)*B.x + t*t*C.x;
		*X2 = NAN;
		return;
	}
	
	float root = sqrt(Y*a + B.y*B.y - A.y*C.y);

	// Solving for t requires +-root. Get a result for both values.
	// If a value of t does not fall in the range of 0<=t<=1, then return NAN. 
	float t = (A.y - B.y + root) / a;
	*X1 = (t > 1 || t < 0 || isnan(t)) ? NAN : (1-t)*(1-t)*A.x + 2*t*(1-t)*B.x + t*t*C.x;
	
	t = (A.y - B.y - root) / a;
	*X2 = (t > 1 || t < 0 || isnan(t)) ? NAN : (1-t)*(1-t)*A.x + 2*t*(1-t)*B.x + t*t*C.x;
}

void bezierIntersectX(glm::vec2 A, glm::vec2 B, glm::vec2 C, float X, float *Y1, float *Y2)
{
	bezierIntersectY(glm::vec2(A.y, A.x), glm::vec2(B.y, B.x), glm::vec2(C.y, C.x), X, Y1, Y2);
}

/*
 * Returns true if the given bezier curve is parallel to and along y=Y.
 * Also returns maximum and minimum x values along Y, if bezier is along Y.
 * Swap all Ys with Xs and this becomes bezierAlongX.
 */
bool bezierAlongY(glm::vec2 A, glm::vec2 B, glm::vec2 C, float Y, float *minX, float *maxX)
{
	if((A.y != B.y) || (A.y != C.y) || (A.y != Y))
		return false;
	
	*minX = glm::min(A.x, B.x);
	*maxX = glm::max(A.x, B.x);
	
	if(B.x > *maxX)
	{
		// TODO, also B.x < minX
		// This probably isn't really necessary, since no font curve is going to have
		// a straight line with a control point, much less a control point outside
		// the bounds of the endpoints.
	}
	
	return true;
}

struct Bezier
{
	glm::vec2 e0;
	glm::vec2 e1;
	glm::vec2 c; // control point
};

/*
 * Returns a list of Bezier curves representing an outline rendered by Freetype.
 * Straight lines are represented with c == e0.
 * The list is ordered in the same order as the outline. That is, outer edges
 * are clockwise, inner edges are couter-clockwise.
 */
std::vector<Bezier> getCurvesForOutline(FT_Outline *outline)
{
	std::vector<Bezier> curves;

	if(outline->n_points <= 0)
		return curves;
		
	// For some reason, the glyphs aren't always positioned with their bottom
	// left corner at 0,0. So find the min x and y values.
	FT_Pos metricsX=outline->points[0].x, metricsY=outline->points[0].y;
	for(short i=1; i<outline->n_points; ++i)
	{
		metricsX = std::min(metricsX, outline->points[i].x);
		metricsY = std::min(metricsY, outline->points[i].y);
	}
	
	// Each contour represents one continuous line made of multiple beziers.
	// The points re-use endpoints as the starting point of the next segment,
	// so its important to set the next curve's e0 to the current curve's e1.
	for(short k=0; k<outline->n_contours; ++k)
	{
		Bezier currentBezier;
		short bezierPartIndex = 0;
		size_t startCurve = curves.size();
		
		for(short i=(k==0)?0:outline->contours[k-1]+1; i<=outline->contours[k]; ++i)
		{
			glm::vec2 p(outline->points[i].x-metricsX, outline->points[i].y-metricsY);
			
		// I know I know, macros are evil. Shhh.
		#define CHECK_BIT(var, pos) ((var) & (1<<(pos)))
			int type = CHECK_BIT(outline->tags[i], 0) ? 0 : 1; // 0 if endpoint, 1 if control point
			if(type == 1)
				type += CHECK_BIT(outline->tags[i], 1) ? 1 : 0; // 1 if regular control point, 2 if third-order control point
		#undef CHECK_BIT
			
			// TODO: Third order control points (cubic beziers) are unsupported.
			// Eventually, convert cubic bezier into two quadratics.
			if(type == 2)
				return std::vector<Bezier>();
			
			if(bezierPartIndex == 0 && type == 0) {
				currentBezier.e0 = p;
				bezierPartIndex++;
			} else if(bezierPartIndex == 0 && type == 1) {
				// printf("ERR1\n");
				return std::vector<Bezier>(); // error
			} else if(bezierPartIndex == 1 && type == 0) {
				currentBezier.c = currentBezier.e0;
				currentBezier.e1 = p;
				curves.push_back(currentBezier);
				// printf("(%.0f, %.0f) to (%.0f, %.0f)\n", currentBezier.e0.x, currentBezier.e0.y, currentBezier.e1.x, currentBezier.e1.y);
				currentBezier.e0 = currentBezier.e1;
			} else if(bezierPartIndex == 1 && type == 1) {
				currentBezier.c = p;
				bezierPartIndex++;
			
			// I'm not sure why type ever equals 1 when it's part index 2
			// since that would imply two control points in a row even though
			// they're not cubic curves, but sometimes it happens. It seems
			// that they should just be treated as not control points.
			} else if((bezierPartIndex == 2 && type == 0) || (bezierPartIndex == 2 && type == 1)) {
				currentBezier.e1 = p;
				curves.push_back(currentBezier);
				// printf("(%.0f, %.0f) to (%.0f, %.0f) through (%.0f, %.0f)\n", currentBezier.e0.x, currentBezier.e0.y, currentBezier.e1.x, currentBezier.e1.y, currentBezier.c.x, currentBezier.c.y);
				currentBezier.e0 = currentBezier.e1;
				bezierPartIndex = 1;
			} else {
				// printf("ERR3\n");
				return std::vector<Bezier>(); // error
			}
		}
		
		size_t endCurve = curves.size();
		size_t numCurves = endCurve - startCurve;
		
		// Connect the final endpoint of the contour to the first startpoint
		// (if it isn't already connected)
		if(numCurves > 0 && curves[startCurve].e0 != curves[endCurve-1].e1)
		{
			Bezier final;
			final.e0 = curves[endCurve-1].e1;
			final.c = final.e0;
			final.e1 = curves[startCurve].e0;
			curves.push_back(final);
		}
	}

	return curves;
}

/*
 * Using a FreeType font face and a unicode code point, convert the glyph
 * into a 2D grid where each grid cell tells the edges of the glyph that
 * intersect it. Since multiple edges can pass through a single cell, it's
 * a grid of std::sets.
 * gridWidth is calculated based on the given grid height and the width of the
 * character. 
 */
bool calculateGridForGlyph(FT_Face face, uint32_t point, uint8_t gridHeight, std::vector<Bezier> *curvesOut, std::vector<std::set<uint16_t>> *gridOut, uint8_t *gridWidthOut)
{
	// Load the glyph. FT_LOAD_NO_SCALE implies that FreeType should not render
	// the glyph to a bitmap, and ensures that metrics and outline points are
	// represented in font units instead of em.
	FT_UInt glyphIndex = FT_Get_Char_Index(face, point);
	if(FT_Load_Glyph(face, glyphIndex, FT_LOAD_NO_SCALE))
		return false;
	
	std::vector<Bezier> curves = getCurvesForOutline(&face->glyph->outline);
	if(curves.size() == 0)
		return false;

	FT_Pos width = face->glyph->metrics.width;
	FT_Pos height = face->glyph->metrics.height;
	
	uint8_t gridWidth = std::max(std::min(width * gridHeight / height, 255L), 1L); 
	*gridWidthOut = gridWidth;
	
	// grid is a linearized 2D array, where each cell stores the indicies
	// of each bezier curve that intersects it. Uses an std::set to remove
	// duplicate intersection indicies.
	std::vector<std::set<uint16_t>> grid;
	grid.resize(gridWidth * gridHeight);

	// For each curve, for each vertical and horizontal grid line
	// (including edges), determine where the curve intersects. Each
	// intersection affects two cells, and each curve can intersect a line
	// up to twice, for a maximum of four cells per line per curve.
	for(size_t i=0; i<curves.size(); ++i)
	{
		for(size_t j=0; j<=gridWidth; ++j)
		{
			float y1, y2;
			bezierIntersectX(curves[i].e0, curves[i].c, curves[i].e1, j * width / gridWidth, &y1, &y2);

			uint8_t y1i = gridHeight - (uint8_t)glm::clamp((signed long)(y1 * gridHeight / height), 0L, (signed long)gridHeight-1) - 1;
			uint8_t y2i = gridHeight - (uint8_t)glm::clamp((signed long)(y2 * gridHeight / height), 0L, (signed long)gridHeight-1) - 1;
			uint8_t x1i = (size_t)std::max((signed long)j-1, (signed long)0);
			uint8_t x2i = (size_t)std::min((signed long)j, (signed long)gridWidth-1);
			
			// TODO: The std::set insert operation is really slow?
			// It appears that this operation nearly doubles the runtime
			// of calculateGridForGlyph.
		#define SETGRID(x, y) { grid[y*gridWidth+x].insert(i); }
			if(!isnan(y1)) { // If an intersection did not occur, val is NAN
				SETGRID(x1i, y1i);
				SETGRID(x2i, y1i);
			}
			if(!isnan(y2)) {
				SETGRID(x1i, y2i);
				SETGRID(x2i, y2i);
			}
		}
		
		for(size_t j=0; j<=gridHeight; ++j)
		{
			float x1, x2;
			bezierIntersectY(curves[i].e0, curves[i].c, curves[i].e1, j * height / gridHeight, &x1, &x2);
			
			uint8_t x1i = (uint8_t)glm::clamp((signed long)(x1 * gridWidth / width), 0L, (signed long)gridWidth-1);
			uint8_t x2i = (uint8_t)glm::clamp((signed long)(x2 * gridWidth / width), 0L, (signed long)gridWidth-1);
			uint8_t y1i = gridHeight - (size_t)std::max((signed long)j-1, (signed long)0) - 1;
			uint8_t y2i = gridHeight - (size_t)std::min((signed long)j, (signed long)gridHeight-1) - 1;
			
			if(!isnan(x1)) {
				SETGRID(x1i, y1i);
				SETGRID(x1i, y2i);
			}
			if(!isnan(x2)) {
				SETGRID(x2i, y1i);
				SETGRID(x2i, y2i);
			}
		#undef SETGRID
		}
	}
	
	*curvesOut = curves;
	*gridOut = grid;
	return true;
}

void getGlyphCurves()
{
	FT_Library ft;
	if(FT_Init_FreeType(&ft) != FT_Err_Ok)
	{
		printf("Failed to load freetype\n");
		return;
	}
	
	FT_Face face;
	
	if(FT_New_Face(ft, "/usr/share/fonts/noto/NotoMono-Regular.ttf", 0, &face))
	{
		printf("Failed to load NotoMono-Regular\n");
		FT_Done_FreeType(ft);
	}
	
	auto start = std::chrono::steady_clock::now();
	
	int C = 256;
	for(int i=0;i<C;++i)
	{
		printf("%i\n", i);
		std::vector<Bezier> curves;
		std::vector<std::set<uint16_t>> grid;
		uint8_t gridWidth=0;
		bool success = calculateGridForGlyph(face, i, 8, &curves, &grid, &gridWidth);
	}
	
	auto end = std::chrono::steady_clock::now();
	
	std::chrono::duration<double> elapsed_seconds = end-start;
	double t = elapsed_seconds.count()*1000;
	printf("total: %fms, per %fms\n", t, t/C);
	
	// printf("\nsuccess: %i, gridWidth: %i\n\n", success, gridWidth);
	
	// if(gridWidth > 0)
	// {
	// 	uint8_t gridHeight = grid.size() / gridWidth;
	// 	size_t x=0;
	// 	for(uint8_t i=0;i<gridHeight;++i)
	// 	{
	// 		for(uint8_t j=0;j<gridWidth;++j)
	// 		{
	// 			if(grid[x].size() == 0)
	// 				printf("   ");
	// 			else
	// 				// printf("%02i", grid[x].size());
	// 				printf("%3d", grid[x].size());
	// 			x++;
	// 		}
	// 		printf("\n");
	// 	}
	// }
	
	FT_Done_Face(face);
	
	FT_Done_FreeType(ft);
	printf("Freetype success\n");
}

void texture2D(bmp *tex, uint8_t channels, uint8_t *out, uint32_t x, uint32_t y)
{
	uint32_t index = ((tex->height-y-1) * tex->width + x) * channels;
	printf("index: %i\n", index);
	if(index+channels >= tex->length)
		return;
	for(uint8_t i=0; i<channels; ++i)
		out[i] = tex->data[index+i];
}

uint16_t ushortFromVec2(uint8_t x, uint8_t y)
{
	return y | x << 8;
}

void fetchVec2(bmp *tex, uint32_t x, uint32_t y, uint16_t *a, uint16_t *b)
{
	uint8_t out[4];
	texture2D(tex, 4, out, x, y);
	*a = ushortFromVec2(out[0], out[1]);
	*b = ushortFromVec2(out[2], out[3]);
}

void initDobbie()
{
	getGlyphCurves();
	
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
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, atlas.width, atlas.height, 0,
		GL_RGBA, GL_UNSIGNED_BYTE, atlas.data);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
	
	// uint32_t *atlasInt = new uint32_t[atlas.length];
	// for(uint32_t i=0;i<atlas.length;++i)
	// 	atlasInt[i] = 255;//atlas.data[i];
	// 
	// glGenBuffers(1, &atlasBuffer);
	// glBindBuffer(GL_SHADER_STORAGE_BUFFER, atlasBuffer);
	// glBufferData(GL_SHADER_STORAGE_BUFFER, atlas.width * atlas.height * 4 * sizeof(uint32_t), atlasInt, GL_STATIC_DRAW);
	// delete [] atlasInt;
	
	numGlyphs = 1;//rawGlyphs.length / 20; // 20 bytes per glyph
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
			
			oCurvesMin[dst+0] = ushortWithFlag(curvesMin[src+0], k & 1);
			oCurvesMin[dst+1] = ushortWithFlag(curvesMin[src+1], k > 1);
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
	oColor[12+0] = 0; oColor[12+1] = 255; oColor[12+2] = 0; oColor[12+3] = 255;
	oColor[24+0] = 0; oColor[24+1] = 0; oColor[24+2] = 255; oColor[24+3] = 255;
	oColor[36+0] = 0; oColor[36+1] = 0; oColor[36+2] = 0; oColor[36+3] = 255;
	oColor[48+0] = 0; oColor[48+1] = 0; oColor[48+2] = 0; oColor[48+3] = 255;
	oColor[60+0] = 0; oColor[60+1] = 0; oColor[60+2] = 0; oColor[60+3] = 255;
	// oCurvesMin[0] = 0;
	// oCurvesMin[1] = 0;
	// oCurvesMin[6+0] = 1;
	// oCurvesMin[6+1] = 0;
	// oCurvesMin[12+0] = 0;
	// oCurvesMin[12+1] = 1;
	oCurvesMin[0] = (0)*2;
	oCurvesMin[1] = (100)*2;
	oCurvesMin[6+0] = (0)*2+1;
	oCurvesMin[6+1] = (100)*2;
	oCurvesMin[12+0] = (0)*2;
	oCurvesMin[12+1] = (100)*2+1;
	
	uint16_t a=0,b=0;
	fetchVec2(&atlas, 0, 66, &a, &b);
	printf("a: %i, b: %i\n", a, b);
	
	uint8_t rgba[4];
	rgba[0] = 0; rgba[1]=0; rgba[2]=0; rgba[3]=0;
	texture2D(&atlas, 4, rgba, 2, 66);
	printf("w: %i, h: %i, l: %i, rgba: %i, %i, %i, %i\n", atlas.width, atlas.height, atlas.length, rgba[0], rgba[1], rgba[2], rgba[3]);

	glGenBuffers(1, &glyphBuffer);
	glBindBuffer(GL_ARRAY_BUFFER, glyphBuffer);
	glBufferData(GL_ARRAY_BUFFER, numGlyphs * 6 * 12, vertexBuf, GL_STATIC_DRAW);
	delete [] vertexBuf;
	
	delete [] rawGlyphs.data;
	delete [] atlas.data;
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
	glEnableVertexAttribArray(3);
	glVertexAttribPointer(0, 2, GL_SHORT, GL_TRUE, 12, (void*)0);
	glVertexAttribPointer(1, 2, GL_UNSIGNED_SHORT, GL_FALSE, 12, (void*)4);
	glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, 12, (void*)8);
	
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, atlasTexId);
	glUniform1i(uAtlasSampler, 0);
	glUniform2f(uTexelSize, 1.0/atlas.width, 1.0/atlas.height);
	glUniform1i(uDebug, 1);
	
	float aspect = (768*1.5) / (1024*1.5);
	float zoomx = 0.05;// std::sin(zoom) + 1.01;
	float zoomy = 0.05;//std::sin(zoom) + 1.01;
	zoomx /= 6.0; zoomy /= 6.0;
	float translateX = 0.429;
	float translateY = 0.596;
	
	glUniform2f(uPositionMul, aspect/zoomx, 1/zoomy);
	glUniform2f(uPositionAdd, aspect * -translateX / zoomx, -translateY / zoomy);
	
	glDrawArrays(GL_TRIANGLES, 0, numGlyphs*6);
}