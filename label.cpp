/*
 * Aidan Shafran <zelbrium@gmail.com>, 2016.
 *
 * This code is based on Will Dobbie's WebGL vector-based text rendering (2016).
 * It can be found here:
 * http://wdobbie.com/post/gpu-text-rendering-with-vector-textures/
 *
 * Dobbie's original code used a pre-generated bezier curve atlas generated
 * from a PDF. This GLLabel class allows for live text rendering based on
 * glyph curves exported from FreeType2.
 *
 * Text is rendered size-independently. This means you can scale, rotate,
 * or reposition text rendered using GLLabel without any loss of quality.
 * All that's required is a font file to load the text from. TTF works well.
 *
 * Dobbie's original code has no attached license, however his comments seem
 * to imply the code is freely available for use. I have contacted him to ask
 * for a license, but received no response. Until further notice, this code is
 * licensed under the Apache Public License v2.0.
 * (Dobbie, please let me know if you have issues with this.)
 */

#include "label.hpp"
#include <set>
#include <fstream>

#define sq(x) ((x)*(x))

static char32_t readNextChar(const char **p, size_t *datalen);
static GLuint loadShaderProgram(const char *vsCodeC, const char *fsCodeC);

std::shared_ptr<GLFontManager> GLFontManager::singleton = nullptr;

namespace {
extern const char *kGlyphVertexShader;
extern const char *kGlyphFragmentShader;
}

static const uint8_t kGridMaxSize = 20; // Grids can be smaller if necessary
static const uint16_t kGridAtlasSize = 256; // Fits exactly 1024 8x8 grids
static const uint16_t kBezierAtlasSize = 128; // Fits around 1024 glyphs +- a few
static const uint8_t kAtlasChannels = 4; // Must be 4 (RGBA), otherwise code breaks

GLLabel::GLLabel()
: pos(0,0), scale(1,1), appendOffset(0,0),
horzAlign(GLLabel::Align::Start),vertAlign(GLLabel::Align::Start),
showingCaret(false)
{
	this->lastColor = {0,0,0,255};
	this->manager = GLFontManager::GetFontManager();
	this->lastFace = this->manager->GetDefaultFont();
	this->manager->LoadASCII(this->lastFace);
	
	glGenBuffers(1, &this->vertBuffer);
}

GLLabel::GLLabel(std::string text) : GLLabel()
{
	this->SetText(text);
}

GLLabel::~GLLabel()
{
	glDeleteBuffers(1, &this->vertBuffer);
}

void GLLabel::SetText(std::string text, FT_Face font, Color color)
{
	this->verts.clear();
	this->AppendText(text, font, color);
}

void GLLabel::AppendText(std::string text, FT_Face face, Color color)
{
	// GlyphVertex q[3];
	// q[0].pos = glm::vec2(0, 10000);
	// q[1].pos = glm::vec2(10000,10000);
	// q[2].pos = glm::vec2(0,0);
	// this->verts.push_back(q[0]);
	// this->verts.push_back(q[1]);
	// this->verts.push_back(q[2]);
	
	size_t prevNumVerts = this->verts.size();
	
	const char *cstr = text.c_str();
	size_t cstrLen = text.size();
	while(cstr[0] != '\0')
	{
		// Get character code point
		char32_t c;
		if((c = readNextChar(&cstr, &cstrLen)) == (char32_t)-1)
		{
			printf("GLLabel::AppendText: Unable to parse UTF-8 string.");
			return;
		}

		if(c == '\r')
			continue;
		
		if(c == '\n')
		{
			this->appendOffset.x = 0;
			this->appendOffset.y = -face->height;
			continue;
		}
		
		GLFontManager::Glyph *glyph = this->manager->GetGlyphForCodepoint(face, c);
		
		GlyphVertex v[6];
		v[0].pos = this->appendOffset;
		v[1].pos = this->appendOffset + glm::vec2(glyph->size.x, 0);
		v[2].pos = this->appendOffset + glm::vec2(0, glyph->size.y);
		v[3].pos = this->appendOffset + glm::vec2(glyph->size.x, glyph->size.y);
		v[4].pos = this->appendOffset + glm::vec2(0, glyph->size.y);
		v[5].pos = this->appendOffset + glm::vec2(glyph->size.x, 0);
		for(unsigned int i=0;i<6;++i)
		{
			v[i].color = color;
			
			// Encode both the bezier position and the norm coord into one int
			// This theoretically could overflow, but the atlas position will
			// never be over half the size of a uint16, so it's fine. 
			unsigned int k = (i < 4) ? i : 6 - i;
			v[i].data[0] = glyph->bezierAtlasPos[0]*2 + ((k & 1) ? 1 : 0);
			v[i].data[1] = glyph->bezierAtlasPos[1]*2 + ((k > 1) ? 1 : 0);
			this->verts.push_back(v[i]);
		}
		
		this->appendOffset.x += glyph->shift;
	}
	
	size_t deltaVerts = this->verts.size() - prevNumVerts;
	if(deltaVerts == 0)
		return;
	
	// TODO: Only upload recent verts
	glBindBuffer(GL_ARRAY_BUFFER, this->vertBuffer);
	glBufferData(GL_ARRAY_BUFFER, this->verts.size() * sizeof(GlyphVertex), &this->verts[0], GL_DYNAMIC_DRAW);
}

void GLLabel::SetHorzAlignment(Align horzAlign)
{	
}
void GLLabel::SetVertAlignment(Align vertAlign)
{
}

void GLLabel::Render(float time)
{
	this->manager->UploadAtlases();
	this->manager->UseGlyphShader();
	
	glBindBuffer(GL_ARRAY_BUFFER, this->vertBuffer);
	glEnable(GL_BLEND);
	
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GLLabel::GlyphVertex), (void*)offsetof(GLLabel::GlyphVertex, pos));
	glVertexAttribPointer(1, 2, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(GLLabel::GlyphVertex), (void*)offsetof(GLLabel::GlyphVertex, data));
	glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GLLabel::GlyphVertex), (void*)offsetof(GLLabel::GlyphVertex, color));
	
	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, this->manager->atlases[0].gridAtlasId);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, this->manager->atlases[0].bezierAtlasId);
	
	this->manager->SetShaderPosScale(glm::vec4(-0.9,-0.9,abs(sin(time/2))/1000.0,abs(sin(time/2))/500.0));
	
	glDrawArrays(GL_TRIANGLES, 0, this->verts.size());
	
	// float aspect = (768*1.5) / (1024*1.5);
	// // float zoomx = std::sin(zoom) + 1.01;
	// // float zoomy = std::sin(zoom) + 1.01;
	// float zoomx = 1.01;
	// float zoomy = 1.01;
	// zoomx /= 100.0; zoomy /= 100.0;
	// float translateX = 0.4295;
	// float translateY = 0.5965;
	// 
	// glUniform2f(uPositionMul, aspect/zoomx, 1/zoomy);
	// glUniform2f(uPositionAdd, aspect * -translateX / zoomx, -translateY / zoomy);
}

void GLLabel::RenderAlso(float time)
{
	
}




GLFontManager::GLFontManager()
{
	if(FT_Init_FreeType(&this->ft) != FT_Err_Ok)
		printf("Failed to load freetype\n");
	
	this->glyphShader = loadShaderProgram(kGlyphVertexShader, kGlyphFragmentShader);
	this->uGridAtlas = glGetUniformLocation(glyphShader, "uGridAtlas");
	this->uBezierAtlas = glGetUniformLocation(glyphShader, "uBezierAtlas");
	this->uGridTexel = glGetUniformLocation(glyphShader, "uGridTexel");
	this->uBezierTexel = glGetUniformLocation(glyphShader, "uBezierTexel");
	this->uPosScale = glGetUniformLocation(glyphShader, "uPosScale");
	
	this->UseGlyphShader();
	glUniform1i(this->uGridAtlas, 0);
	glUniform1i(this->uBezierAtlas, 1);
	glUniform2f(this->uGridTexel, 1.0/kGridAtlasSize, 1.0/kGridAtlasSize);
	glUniform2f(this->uBezierTexel, 1.0/kBezierAtlasSize, 1.0/kBezierAtlasSize);
	glUniform4f(this->uPosScale, 0, 0, 1, 1);
}

GLFontManager::~GLFontManager()
{
	glDeleteProgram(this->glyphShader);
	FT_Done_FreeType(this->ft);
}

std::shared_ptr<GLFontManager> GLFontManager::GetFontManager()
{
	if(!GLFontManager::singleton)
		GLFontManager::singleton = std::shared_ptr<GLFontManager>(new GLFontManager());
	return GLFontManager::singleton;
}

// TODO: FT_Faces don't get destroyed... FT_Done_FreeType cleans them eventually,
// but maybe use shared pointers?
FT_Face GLFontManager::GetFontFromPath(std::string fontPath)
{
	FT_Face face;
	return FT_New_Face(this->ft, fontPath.c_str(), 0, &face) ? nullptr : face;
}
FT_Face GLFontManager::GetFontFromName(std::string fontName)
{
	std::string path; // TODO
	return GLFontManager::GetFontFromPath(path);
}
FT_Face GLFontManager::GetDefaultFont()
{
	// TODO
	return GLFontManager::GetFontFromPath("/usr/share/fonts/noto/NotoSans-Regular.ttf");
}

GLFontManager::AtlasGroup * GLFontManager::GetOpenAtlasGroup()
{
	if(this->atlases.size() == 0 || this->atlases[this->atlases.size()-1].full)
	{
		AtlasGroup group = {0};
		group.bezierAtlas = new uint8_t[sq(kBezierAtlasSize)*kAtlasChannels]();
		group.gridAtlas = new uint8_t[sq(kGridAtlasSize)*kAtlasChannels]();
		group.uploaded = true;
		
		glGenTextures(1, &group.bezierAtlasId);
		glBindTexture(GL_TEXTURE_2D, group.bezierAtlasId);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kBezierAtlasSize, kBezierAtlasSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, group.bezierAtlas);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		
		glGenTextures(1, &group.gridAtlasId);
		glBindTexture(GL_TEXTURE_2D, group.gridAtlasId);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kGridAtlasSize, kGridAtlasSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, group.gridAtlas);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
		
		this->atlases.push_back(group);
	}
	
	return &this->atlases[this->atlases.size()-1];
}

struct Bezier
{
	glm::vec2 e0;
	glm::vec2 e1;
	glm::vec2 c; // control point
};

inline bool almostEqual(float a, float b)
{
	return std::fabs(a-b) < 1e-5;
}

/*
 * Taking a quadratic bezier curve and a horizontal line y=Y, finds the x
 * values of intersection of the line and the curve. Returns 0, 1, or 2,
 * depending on how many intersections were found, and outX is filled with
 * that many x values of intersection.
 *
 * Quadratic bezier curves are represented by the function
 * F(t) = (1-t)^2*A + 2*t*(1-t)*B + t^2*C
 * where F is a vector function, A and C are the endpoint vectors, C is
 * the control point vector, and 0 <= t <= 1.
 * Solving the bezier function for t gives:
 * t = (A - B [+-] sqrt(y*a + B^2 - A*C))/a , where  a = A - 2B + C.
 * http://www.wolframalpha.com/input/?i=y+%3D+(1-t)%5E2a+%2B+2t(1-t)*b+%2B+t%5E2*c+solve+for+t
 */
static int bezierIntersectHorz(Bezier *curve, glm::vec2 *outX, float Y)
{
	glm::vec2 A = curve->e0;
	glm::vec2 B = curve->c;
	glm::vec2 C = curve->e1;
	int i = 0;

#define T_VALID(t) ((t) <= 1 && (t) >= 0)
#define X_FROM_T(t) ((1-(t))*(1-(t))*curve->e0.x + 2*(t)*(1-(t))*curve->c.x + (t)*(t)*curve->e1.x)

	// Parts of the bezier function solved for t
	float a = curve->e0.y - 2*curve->c.y + curve->e1.y;

	// In the condition that a=0, the standard formulas won't work
	if(almostEqual(a, 0))
	{
		float t = (2*B.y - C.y - Y) / (2*(B.y-C.y));
		if(T_VALID(t))
			(*outX)[i++] = X_FROM_T(t);
		return i;
	}
	
	float sqrtTerm = sqrt(Y*a + B.y*B.y - A.y*C.y);
	
	float t = (A.y - B.y + sqrtTerm) / a;
	if(T_VALID(t))
		(*outX)[i++] = X_FROM_T(t);
	
	t = (A.y - B.y - sqrtTerm) / a;
	if(T_VALID(t))
		(*outX)[i++] = X_FROM_T(t);

	return i;

#undef X_FROM_T
#undef T_VALID
}

/*
 * Same as bezierIntersectHorz, except finds the y values of an intersection
 * with the vertical line x=X.
 */
static int bezierIntersectVert(Bezier *curve, glm::vec2 *outY, float X)
{
	Bezier inverse = {
		glm::vec2(curve->e0.y, curve->e0.x),
		glm::vec2(curve->e1.y, curve->e1.x),
		glm::vec2(curve->c.y, curve->c.x)
	};
	return bezierIntersectHorz(&inverse, outY, X);
}

struct OutlineDecomposeState
{
	FT_Vector prevPoint;
	std::vector<Bezier> *curves;
	FT_Pos metricsX;
	FT_Pos metricsY;
};

/*
 * Uses FreeType's outline decomposing to convert an outline into a vector
 * of beziers. This just makes working with the outline easier.
 */
static std::vector<Bezier> GetCurvesForOutline(FT_Outline *outline)
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

	OutlineDecomposeState state = {0};
	state.curves = &curves;
	state.metricsX = metricsX;
	state.metricsY = metricsY;

	FT_Outline_Funcs funcs = {0};
	funcs.move_to = [](const FT_Vector *to, void *user) -> int {
		auto state = static_cast<OutlineDecomposeState *>(user);
		state->prevPoint = *to;
		return 0;
	};
	funcs.line_to = [](const FT_Vector *to, void *user) -> int {
		auto state = static_cast<OutlineDecomposeState *>(user);
		Bezier b;
		b.e0 = glm::vec2(state->prevPoint.x - state->metricsX, state->prevPoint.y - state->metricsY);
		b.c = b.e0;
		b.e1 = glm::vec2(to->x - state->metricsX, to->y - state->metricsY);
		state->curves->push_back(b);
		state->prevPoint = *to;
		return 0;
	};
	funcs.conic_to = [](const FT_Vector *control, const FT_Vector *to, void *user) -> int {
		auto state = static_cast<OutlineDecomposeState *>(user);
		Bezier b;
		b.e0 = glm::vec2(state->prevPoint.x - state->metricsX, state->prevPoint.y - state->metricsY);
		b.c = glm::vec2(control->x - state->metricsX, control->y - state->metricsY);
		b.e1 = glm::vec2(to->x - state->metricsX, to->y - state->metricsY);
		state->curves->push_back(b);
		state->prevPoint = *to;
		return 0;
	};
	funcs.cubic_to = [](const FT_Vector *control1, const FT_Vector *control2, const FT_Vector *to, void *user) -> int {
		// Not implemented
		return -1;
	};

	if(FT_Outline_Decompose(outline, &funcs, &state) == 0)
		return curves;
	return std::vector<Bezier>();
}

/*
 * Using the outline curves of a glyph, creates a square grid of edge length
 * GLLabel::kGridMaxSize where each cell stores all of the indices of the
 * curves that intersect that cell.
 * The grid is returned as a lineariezed 2D array.
 */
static std::vector<std::set<uint16_t>> GetGridForCurves(std::vector<Bezier> &curves, FT_Pos glyphWidth, FT_Pos glyphHeight, uint8_t &gridWidth, uint8_t &gridHeight)
{
	gridWidth = kGridMaxSize;
	gridHeight = kGridMaxSize;
	
	std::vector<std::set<uint16_t>> grid;
	grid.resize(gridWidth * gridHeight);
	
	// For each curve, for each vertical and horizontal grid line
	// (including edges), determine where the curve intersects. Each
	// intersection affects two cells, and each curve can intersect a line
	// up to twice, for a maximum of four cells per line per curve.
	for(uint32_t i=0;i<curves.size();++i)
	{
		// TODO: The std::set insert operation is really slow?
		// It appears that this operation nearly doubles the runtime
		// of calculateGridForGlyph.
		#define SETGRID(x, y) { grid[(y)*gridWidth+(x)].insert(i); }
		
		// If a curve intersects no grid lines, it won't be included. So
		// make sure the cell the the curve starts in is included
		SETGRID(std::min((unsigned long)(curves[i].e0.x * gridWidth / glyphWidth), (unsigned long)gridWidth-1), std::min((unsigned long)(curves[i].e0.y * gridHeight / glyphHeight), (unsigned long)gridHeight-1));

		for(size_t j=0; j<=gridWidth; ++j)
		{
			glm::vec2 intY;
			int num = bezierIntersectVert(&curves[i], &intY, j * glyphWidth / gridWidth);
			
			for(int z=0;z<num;++z)
			{
				uint8_t y = (uint8_t)glm::clamp((signed long)(intY[z] * gridHeight / glyphHeight), 0L, (signed long)gridHeight-1);
				uint8_t x1 = (size_t)std::max((signed long)j-1, (signed long)0);
				uint8_t x2 = (size_t)std::min((signed long)j, (signed long)gridWidth-1);
				SETGRID(x1, y);
				SETGRID(x2, y);
			}
		}
		
		for(size_t j=0; j<=gridHeight; ++j)
		{
			glm::vec2 intX;
			int num = bezierIntersectHorz(&curves[i], &intX, j * glyphHeight / gridHeight);
			
			for(int z=0;z<num;++z)
			{
				uint8_t x = (uint8_t)glm::clamp((signed long)(intX[z] * gridWidth / glyphWidth), 0L, (signed long)gridWidth-1);
				uint8_t y1 = (size_t)std::max((signed long)j-1, (signed long)0);
				uint8_t y2 = (size_t)std::min((signed long)j, (signed long)gridHeight-1);
				SETGRID(x, y1);
				SETGRID(x, y2);
			}
		}
		#undef SETGRID
	}
	
	// In order for the shader to know whether a cell that has no intersections
	// is within or outside the glyph, a flag is stored in each cell telling
	// whether the center of the cell is inside or not.
	
	std::set<float> intersections; // Sets keep ordered (avoids calling sort)
	for(size_t i=0; i<gridHeight; ++i)
	{
		intersections.clear();
	
		float Y = i + 0.5; // Test midpoints of cells
		for(size_t j=0; j<curves.size(); ++j)
		{
			glm::vec2 intX;
			int num = bezierIntersectHorz(&curves[j], &intX, Y * glyphHeight / gridHeight);
			for(int z=0;z<num;++z)
				intersections.insert(intX[z] * gridWidth / glyphWidth);
		}

		// TODO: Necessary? Should never be required on a closed curve.
		// (Make sure last intersection is >= gridWidth)
		// if(*intersections.rbegin() < gridWidth)
		// 	intersections.insert(gridWidth);

		bool inside = false;
		float start = 0;
		for(auto it=intersections.begin(); it!=intersections.end(); it++)
		{
			float end = *it;
			// printf("row %i intersection [%f, %f]\n", i, start, end);
			if(inside)
			{
				size_t roundS = glm::clamp(round(start), (double)0.0, (double)(gridWidth));
				size_t roundE = glm::clamp(round(end), (double)0.0, (double)(gridWidth));
				// printf("inside, %i, %i\n", roundS, roundE);
				
				for(size_t k=roundS;k<roundE;++k)
				{
					size_t gridIndex = i*gridWidth + k;
					grid[gridIndex].insert(254); // Becomes 255 after +1 to remove 0s
				}
			}
			inside = !inside;
			start = end;
		}
	}

	return grid;
}

GLFontManager::Glyph * GLFontManager::GetGlyphForCodepoint(FT_Face face, uint32_t point)
{
	auto faceIt = this->glyphs.find(face);
	if(faceIt != this->glyphs.end())
	{
		auto glyphIt = faceIt->second.find(point);
		if(glyphIt != faceIt->second.end())
			return &glyphIt->second;
	}
	
	AtlasGroup *atlas = this->GetOpenAtlasGroup();

	// Load the glyph. FT_LOAD_NO_SCALE implies that FreeType should not
	// render the glyph to a bitmap, and ensures that metrics and outline
	// points are represented in font units instead of em.
	FT_UInt glyphIndex = FT_Get_Char_Index(face, point);
	if(FT_Load_Glyph(face, glyphIndex, FT_LOAD_NO_SCALE))
		return nullptr;
	
	FT_Pos glyphWidth = face->glyph->metrics.width;
	FT_Pos glyphHeight = face->glyph->metrics.height;
	uint8_t gridWidth, gridHeight;
	
	std::vector<Bezier> curves = GetCurvesForOutline(&face->glyph->outline);
	std::vector<std::set<uint16_t>> grid = GetGridForCurves(curves, glyphWidth, glyphHeight, gridWidth, gridHeight);
	
	// Although the data is represented as a 32bit texture, it's actually
	// two 16bit ints per pixel, each with an x and y coordinate for
	// the bezier. Every six 16bit ints (3 pixels) is a full bezier
	// Plus two pixels for grid position information
	uint16_t bezierPixelLength = 2 + curves.size()*3;
	
	if(curves.size() == 0 || grid.size() == 0 || bezierPixelLength > kBezierAtlasSize)
	{
		if(bezierPixelLength > kBezierAtlasSize)
			printf("WARNING: Glyph %i has too many curves\n", point);

		GLFontManager::Glyph glyph = {0};
		glyph.atlasIndex = -1;
		glyph.size = glm::vec2(glyphWidth, glyphHeight);
		glyph.shift = face->glyph->advance.x;
		this->glyphs[face][point] = glyph;
		return &this->glyphs[face][point];
	}
	
	// Find an open position in the bezier atlas
	if(atlas->nextBezierPos[0] + bezierPixelLength > kBezierAtlasSize)
	{
		// Next row
		atlas->nextBezierPos[1] ++;
		atlas->nextBezierPos[0] = 0;
		if(atlas->nextBezierPos[1] >= kBezierAtlasSize)
		{
			atlas->full = true;
			atlas->uploaded = false;
			atlas = this->GetOpenAtlasGroup();
		}
	}
	
	// Find an open position in the grid atlas
	if(atlas->nextGridPos[0] + kGridMaxSize > kGridAtlasSize)
	{
		atlas->nextGridPos[1] ++;
		atlas->nextGridPos[0] = 0;
		if(atlas->nextGridPos[1] >= kGridAtlasSize)
		{
			atlas->full = true;
			atlas->uploaded = false;
			atlas = this->GetOpenAtlasGroup(); // Should only ever happen once per glyph
		}
	}
	
	uint8_t *bezierData = atlas->bezierAtlas + (atlas->nextBezierPos[1]*kBezierAtlasSize + atlas->nextBezierPos[0])*kAtlasChannels;
	uint16_t *bezierData16 = (uint16_t *)bezierData;
	
	// TODO: The shader combines each set of bytes into 16 bit ints, which
	// depends on endianness. So this currently only works on little-endian
	bezierData16[0] = atlas->nextGridPos[0];
	bezierData16[1] = atlas->nextGridPos[1];
	bezierData16[2] = kGridMaxSize;
	bezierData16[3] = kGridMaxSize;
	bezierData16 += 4; // 2 pixels
	for(uint32_t j=0;j<curves.size();++j)
	{
		// 3 pixels = 6 uint16s
		// Scale coords from [0,glyphSize] to [0,maxUShort]
		bezierData16[j*6+0] = curves[j].e0.x * 65535 / glyphWidth;
		bezierData16[j*6+1] = curves[j].e0.y * 65535 / glyphHeight;
		bezierData16[j*6+2] = curves[j].c.x  * 65535 / glyphWidth;
		bezierData16[j*6+3] = curves[j].c.y  * 65535 / glyphHeight;
		bezierData16[j*6+4] = curves[j].e1.x * 65535 / glyphWidth;
		bezierData16[j*6+5] = curves[j].e1.y * 65535 / glyphHeight;
	}
	
	// Copy grid to atlas
	for(uint32_t y=0;y<gridHeight;++y)
	{
		for(uint32_t x=0;x<gridWidth;++x)
		{
			size_t gridIdx = y*gridWidth + x;
			size_t gridmapIdx = ((atlas->nextGridPos[1]+y)*kGridAtlasSize + (atlas->nextGridPos[0]+x))*kAtlasChannels;
			
			size_t j = 0;
			for(auto it=grid[gridIdx].begin(); it!=grid[gridIdx].end(); ++it)
			{
				if(j >= kAtlasChannels) // TODO: More than four beziers per pixel?
				{
					printf("MORE THAN 4 on %i\n", point);
					break;
				}
				atlas->gridAtlas[gridmapIdx+j] = *it + 1;
				j++;
			}
		}
	}
	
	GLFontManager::Glyph glyph = {0};
	glyph.bezierAtlasPos[0] = atlas->nextBezierPos[0];
	glyph.bezierAtlasPos[1] = atlas->nextBezierPos[1];
	glyph.atlasIndex = this->atlases.size()-1;
	glyph.size = glm::vec2(glyphWidth, glyphHeight);
	glyph.shift = face->glyph->advance.x;
	// glyph.offset = 
	// glyph.kern = 
	this->glyphs[face][point] = glyph;
	
	atlas->nextBezierPos[0] += bezierPixelLength;
	atlas->nextGridPos[0] += kGridMaxSize;
	atlas->uploaded = false;
	
	return &this->glyphs[face][point];
}

void GLFontManager::LoadASCII(FT_Face face)
{
	if(!face)
		return;
	
	// this->GetGlyphForCodepoint(face, 0);
	// for(int i=32; i<128; ++i)
	// 	this->GetGlyphForCodepoint(face, i);
}

void GLFontManager::UploadAtlases()
{
	for(size_t i=0;i<this->atlases.size();++i)
	{
		if(this->atlases[i].uploaded)
			continue;
		glBindTexture(GL_TEXTURE_2D, this->atlases[i].bezierAtlasId);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kBezierAtlasSize, kBezierAtlasSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, this->atlases[i].bezierAtlas);
		glBindTexture(GL_TEXTURE_2D, this->atlases[i].gridAtlasId);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kGridAtlasSize, kGridAtlasSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, this->atlases[i].gridAtlas);
		atlases[i].uploaded = true;
	}
}

void GLFontManager::UseGlyphShader()
{
	glUseProgram(this->glyphShader);
}

void GLFontManager::SetShaderPosScale(glm::vec4 posScale)
{
	glUniform4f(this->uPosScale, posScale.x, posScale.y, posScale.z, posScale.w);
}

static char32_t readNextChar(const char **p, size_t *datalen)
{
	// Gets the next Unicode Code Point from a UTF8 encoded string
	// (Basically converts UTF8 to UTF32 one char at a time)
	// http://stackoverflow.com/questions/2948308/how-do-i-read-utf-8-characters-via-a-pointer/2953960#2953960
	
#define IS_IN_RANGE(c, f, l)    (((c) >= (f)) && ((c) <= (l)))
	
	unsigned char c1, c2, *ptr = (unsigned char*)(*p);
	char32_t uc = 0;
	int seqlen;
	// int datalen = ... available length of p ...;
	
	if( (*datalen) < 1 )
	{
		// malformed data, do something !!!
		return (char32_t)-1;
	}
	
	c1 = ptr[0];
	
	if((c1 & 0x80) == 0)
	{
		uc = (char32_t)(c1 & 0x7F);
		seqlen = 1;
	}
	else if((c1 & 0xE0) == 0xC0)
	{
		uc = (char32_t)(c1 & 0x1F);
		seqlen = 2;
	}
	else if((c1 & 0xF0) == 0xE0)
	{
		uc = (char32_t)(c1 & 0x0F);
		seqlen = 3;
	}
	else if((c1 & 0xF8) == 0xF0)
	{
		uc = (char32_t)(c1 & 0x07);
		seqlen = 4;
	}
	else
	{
		// malformed data, do something !!!
		return (char32_t)-1;
	}
	
	if( seqlen > (*datalen) )
	{
		// malformed data, do something !!!
		return (char32_t) -1;
	}
	
	for(int i = 1; i < seqlen; ++i)
	{
		c1 = ptr[i];
		
		if((c1 & 0xC0) != 0x80)
		{
			// malformed data, do something !!!
			return (char32_t)-1;
		}
	}
	
	switch(seqlen)
	{
		case 2:
		{
			c1 = ptr[0];
			
			if(!IS_IN_RANGE(c1, 0xC2, 0xDF))
			{
				// malformed data, do something !!!
				return (char32_t)-1;
			}
			
			break;
		}
			
		case 3:
		{
			c1 = ptr[0];
			c2 = ptr[1];
			
			if(((c1 == 0xE0) && !IS_IN_RANGE(c2, 0xA0, 0xBF)) ||
			   ((c1 == 0xED) && !IS_IN_RANGE(c2, 0x80, 0x9F)) ||
			   (!IS_IN_RANGE(c1, 0xE1, 0xEC) && !IS_IN_RANGE(c1, 0xEE, 0xEF)))
			{
				// malformed data, do something !!!
				return (char32_t)-1;
			}
			
			break;
		}
			
		case 4:
		{
			c1 = ptr[0];
			c2 = ptr[1];
			
			if(((c1 == 0xF0) && !IS_IN_RANGE(c2, 0x90, 0xBF)) ||
			   ((c1 == 0xF4) && !IS_IN_RANGE(c2, 0x80, 0x8F)) ||
			   !IS_IN_RANGE(c1, 0xF0, 0xF4)) //0xF1, 0xF3) )		// Modified because originally, no 4-byte characters would pass
			{
				// malformed data, do something !!!
				return (char32_t)-1;
			}
			
			break;
		}
	}
	
	for(int i = 1; i < seqlen; ++i)
	{
		uc = ((uc << 6) | (char32_t)(ptr[i] & 0x3F));
	}
	
#undef IS_IN_RANGE
	
	(*p) += seqlen;
	(*datalen) += seqlen;
	return uc;
}

static GLuint loadShaderProgram(const char *vsCodeC, const char *fsCodeC)
{
	// Compile vertex shader
	GLuint vertexShaderId = glCreateShader(GL_VERTEX_SHADER);
	glShaderSource(vertexShaderId, 1, &vsCodeC, NULL);
	glCompileShader(vertexShaderId);
	
	GLint result = GL_FALSE;
	int infoLogLength = 0;
	glGetShaderiv(vertexShaderId, GL_COMPILE_STATUS, &result);
	glGetShaderiv(vertexShaderId, GL_INFO_LOG_LENGTH, &infoLogLength);
	if(infoLogLength > 1)
	{
		std::vector<char> infoLog(infoLogLength+1);
		glGetShaderInfoLog(vertexShaderId, infoLogLength, NULL, &infoLog[0]);
		printf("[Vertex] %s\n", &infoLog[0]);
	}
	if(!result)
		return 0;

	// Compile fragment shader
	GLuint fragmentShaderId = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragmentShaderId, 1, &fsCodeC, NULL);
	glCompileShader(fragmentShaderId);
	
	result = GL_FALSE, infoLogLength = 0;
	glGetShaderiv(fragmentShaderId, GL_COMPILE_STATUS, &result);
	glGetShaderiv(fragmentShaderId, GL_INFO_LOG_LENGTH, &infoLogLength);
	if(infoLogLength > 1)
	{
		std::vector<char> infoLog(infoLogLength);
		glGetShaderInfoLog(fragmentShaderId, infoLogLength, NULL, &infoLog[0]);
		printf("[Fragment] %s\n", &infoLog[0]);
	}
	if(!result)
		return 0;
	
	// Link the program
	GLuint programId = glCreateProgram();
	glAttachShader(programId, vertexShaderId);
	glAttachShader(programId, fragmentShaderId);
	glLinkProgram(programId);

	result = GL_FALSE, infoLogLength = 0;
	glGetProgramiv(programId, GL_LINK_STATUS, &result);
	glGetProgramiv(programId, GL_INFO_LOG_LENGTH, &infoLogLength);
	if(infoLogLength > 1)
	{
		std::vector<char> infoLog(infoLogLength+1);
		glGetProgramInfoLog(programId, infoLogLength, NULL, &infoLog[0]);
		printf("[Shader Linker] %s\n", &infoLog[0]);
	}
	if(!result)
		return 0;

	glDetachShader(programId, vertexShaderId);
	glDetachShader(programId, fragmentShaderId);
	
	glDeleteShader(vertexShaderId);
	glDeleteShader(fragmentShaderId);

	return programId;
}

namespace {
const char *kGlyphVertexShader = R"(
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
)";

const char *kGlyphFragmentShader = R"(
// This shader slightly modified from source code by Will Dobbie.
// See dobbieText.cpp for more info.

#version 330 core
precision highp float;

#define numSS 4
#define pi 3.1415926535897932384626433832795
#define kPixelWindowSize 1.0

uniform sampler2D uGridAtlas;
uniform sampler2D uBezierAtlas;
uniform vec2 uGridTexel;
uniform vec2 uBezierTexel;

varying vec4 oColor;
varying vec2 oBezierCoord;
varying vec2 oNormCoord;
varying vec4 oGridRect;

float positionAt(float p0, float p1, float p2, float t) {
	float mt = 1.0 - t;
	return mt*mt*p0 + 2.0*t*mt*p1 + t*t*p2;
}

float tangentAt(float p0, float p1, float p2, float t) {
	return 2.0 * (1.0-t) * (p1 - p0) + 2.0 * t * (p2 - p1);
}

bool almostEqual(float a, float b) {
	return abs(a-b) < 1e-5;
}

float normalizedUshortFromVec2(vec2 v)
{
	return (v.y * 65280.0 + v.x * 255.0) / 65536.0;
}

vec4 getPixelByXY(vec2 coord)
{
	return texture2D(uBezierAtlas, (coord+0.5)*uBezierTexel);
}

void fetchBezier(int coordIndex, out vec2 p[3]) {
	for (int i=0; i<3; i++) {
		vec4 pixel = getPixelByXY(vec2(oBezierCoord.x + 2 + coordIndex*3 + i, oBezierCoord.y));
		p[i] = vec2(normalizedUshortFromVec2(pixel.xy), normalizedUshortFromVec2(pixel.zw)) - oNormCoord;
	}
}

int getAxisIntersections(float p0, float p1, float p2, out vec2 t) {
    if (almostEqual(p0, 2.0*p1 - p2)) {
        t[0] = 0.5 * (p2 - 2.0*p1) / (p2 - p1);
        return 1;
    }

    float sqrtTerm = p1*p1 - p0*p2;
    if (sqrtTerm < 0.0) return 0;
    sqrtTerm = sqrt(sqrtTerm);
    float denom = p0 - 2.0*p1 + p2;
    t[0] = (p0 - p1 + sqrtTerm) / denom;
    t[1] = (p0 - p1 - sqrtTerm) / denom;
    return 2;
}

float integrateWindow(float x) {
    float xsq = x*x;
    return sign(x) * (0.5 * xsq*xsq - xsq) + 0.5;           // parabolic window
    //return 0.5 * (1.0 - sign(x) * xsq);                     // box window
}

mat2 getUnitLineMatrix(vec2 b1, vec2 b2) {
    vec2 V = b2 - b1;
    float normV = length(V);
    V = V / (normV*normV);

    return mat2(V.x, -V.y, V.y, V.x);
}

void updateClosestCrossing(in vec2 porig[3], mat2 M, inout float closest, vec2 integerCell) {

	vec2 p[3];
    for (int i=0; i<3; i++) {
        p[i] = M * porig[i];
    }

    vec2 t;
    int numT = getAxisIntersections(p[0].y, p[1].y, p[2].y, t);

    for (int i=0; i<2; i++) {
        if (i == numT) break;
        if (t[i] > 0.0 && t[i] < 1.0) {
            float posx = positionAt(p[0].x, p[1].x, p[2].x, t[i]);
            vec2 op = vec2(positionAt(porig[0].x, porig[1].x, porig[2].x, t[i]),
            			   positionAt(porig[0].y, porig[1].y, porig[2].y, t[i]));
            op += oNormCoord;

            bool sameCell = floor( clamp(op * oGridRect.zw, vec2(0.5), vec2(oGridRect.zw)-0.5)) == integerCell;

            //if (posx > 0.0 && posx < 1.0 && posx < abs(closest)) {
        	if (sameCell && abs(posx) < abs(closest)) {
                float derivy = tangentAt(p[0].y, p[1].y, p[2].y, t[i]);
                closest = (derivy < 0.0) ? -posx : posx;
            }
        }
    }
}

mat2 inverse(mat2 m) {
  return mat2(m[1][1],-m[0][1],
             -m[1][0], m[0][0]) / (m[0][0]*m[1][1] - m[0][1]*m[1][0]);
}


void main()
{
    vec2 integerCell = floor(clamp(oNormCoord * oGridRect.zw, vec2(0.5), vec2(oGridRect.zw)-0.5));
    vec2 indicesCoord = oGridRect.xy + integerCell + 0.5;
    vec2 cellMid = (integerCell + 0.5) / oGridRect.zw;

    mat2 initrot = inverse(mat2(dFdx(oNormCoord) * kPixelWindowSize, dFdy(oNormCoord) * kPixelWindowSize));

    float theta = pi/float(numSS);
    mat2 rotM = mat2(cos(theta), sin(theta), -sin(theta), cos(theta));      // note this is column major ordering

    ivec4 indices1;
    indices1 = ivec4(texture2D(uGridAtlas, indicesCoord*uGridTexel) * 255.0 + 0.5);
    // indices2 = ivec4(texture2D(uAtlasSampler, vec2(indicesCoord.x + vGridSize.x, indicesCoord.y) * uTexelSize) * 255.0 + 0.5);

    // bool moreThanFourIndices = indices1[0] < indices1[1];
	
	float midClosest = (indices1[0] > 250 || indices1[1] > 250 || indices1[2] > 250 || indices1[3] > 250) ? -2.0 : 2.0;
	
    float firstIntersection[numSS];
    for (int ss=0; ss<numSS; ss++) {
        firstIntersection[ss] = 2.0;
    }

    float percent = 0.0;

    mat2 midTransform = getUnitLineMatrix(oNormCoord, cellMid);

    for (int bezierIndex=0; bezierIndex<4; bezierIndex++) {
        int coordIndex;

        // if (bezierIndex < 4) {
            coordIndex = indices1[bezierIndex];
        // } else {
        //     if (!moreThanFourIndices) break;
        //     coordIndex = indices2[bezierIndex-4];
        // }

        if (coordIndex == 0 || coordIndex > 250) {
            continue;
        }

        vec2 p[3];
        fetchBezier(coordIndex-1, p);
		
        updateClosestCrossing(p, midTransform, midClosest, integerCell);

        // Transform p so fragment in glyph space is a unit circle
        for (int i=0; i<3; i++) {
            p[i] = initrot * p[i];
        }

        // Iterate through angles
        for (int ss=0; ss<numSS; ss++) {
            vec2 t;
            int numT = getAxisIntersections(p[0].x, p[1].x, p[2].x, t);
		
            for (int tindex=0; tindex<2; tindex++) {
                if (tindex == numT) break;
		
                if (t[tindex] > 0.0 && t[tindex] <= 1.0) {
		
                    float derivx = tangentAt(p[0].x, p[1].x, p[2].x, t[tindex]);
                    float posy = positionAt(p[0].y, p[1].y, p[2].y, t[tindex]);
		
                    if (posy > -1.0 && posy < 1.0) {
                        // Note: whether to add or subtract in the next statement is determined
                        // by which convention the path uses: moving from the bezier start to end, 
                        // is the inside to the right or left?
                        // The wrong operation will give buggy looking results, not a simple inverse.
                        float delta = integrateWindow(posy);
                        percent = percent + (derivx < 0.0 ? delta : -delta);
		
                        float intersectDist = posy + 1.0;
                        if (intersectDist < abs(firstIntersection[ss])) {
                            firstIntersection[ss] = derivx < 0.0 ? -intersectDist : intersectDist;
                        }
                    }
                }
            }
		
            if (ss+1<numSS) {
                for (int i=0; i<3; i++) {
                    p[i] = rotM * p[i];
                }
            }
        }   // ss
    }

    bool midVal = midClosest < 0.0;

    // Add contribution from rays that started inside
    for (int ss=0; ss<numSS; ss++) {
        if ((firstIntersection[ss] >= 2.0 && midVal) || (firstIntersection[ss] > 0.0 && abs(firstIntersection[ss]) < 2.0)) {
            percent = percent + 1.0 /*integrateWindow(-1.0)*/;
        }
    }

    percent = percent / float(numSS);
	gl_FragColor = oColor;
	gl_FragColor.a *= percent;
	// gl_FragColor.a += 0.2;
	
	// // gl_FragColor.r = uBezierTexel.y*128;
	// gl_FragColor.r = oGridRect.y;
	// // gl_FragColor.g = oGridRect.y;
	// // gl_FragColor.b = oGridRect.z;
	
	// vec2 h = oGridRect.xy + oNormCoord*oGridRect.zw;
	// vec2 l = vec2((h.x+0.5)*uGridTexel.x, (h.y+0.5)*uGridTexel.y);
	// 
	
	// gl_FragColor = getPixelByXY(vec2(oBezierCoord.x + oNormCoord, oBezierCoord.y));

	
	// gl_FragColor = texture2D(uGridAtlas, l);
	// gl_FragColor.a = 1;
}
)";
}