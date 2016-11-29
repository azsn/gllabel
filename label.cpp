#include "label.hpp"
#include <set>

#define sq(x) ((x)*(x))

static char32_t readNextChar(const char **p, size_t *datalen);

static const uint8_t kGridMaxSize = 8; // Grids can be smaller if necessary
static const uint16_t kGridAtlasSize = 256; // Fits exactly 1024 8x8 grids
static const uint16_t kBezierAtlasSize = 512; // Fits around 1024 glyphs +- a few
static const uint8_t kAtlasChannels = 4; // Must be 4 (RGBA), otherwise code breaks

GLLabel::GLLabel()
: GLLabel("")
{
}

GLLabel::GLLabel(std::string text)
: x(0), y(0), horzAlign(GLLabel::Align::Start), vertAlign(GLLabel::Align::Start), showingCursor(false)
{
	this->SetText(text);
}

GLLabel::~GLLabel()
{
	
}

void GLLabel::SetText(std::string text)
{
	this->verts.clear();
	this->AppendText(text);
}

void GLLabel::AppendText(std::string text)
{
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
		
		// switch(c)
		// {
		// case '\n':
		// 	this->glyphs.push_back(nullptr);
		// 	break;
		// case '\r': break; // Ignore \r
		// default:
		// 	GLGlyph *glyph = this->cache->GetGlpyhForCodePoint(c, 12);
		// 	if(glyph)
		// 		this->glyphs.push_back(glyph);
		// 	break;
		// }
	}
}

void GLLabel::Render(float time)
{
	
}

GLLabel::AtlasGroup * GLLabel::GetOpenAtlasGroup()
{
	if(this->atlases.size() == 0 || this->atlases[this->atlases.size()-1].full)
	{
		AtlasGroup group = {0};
		group.bezierAtlas = new uint8_t[sq(kBezierAtlasSize)*kAtlasChannels]();
		group.gridAtlas = new uint8_t[sq(kGridAtlasSize)*kAtlasChannels]();
		
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

	if(FT_Outline_Decompose(outline, &funcs, NULL) == 0)
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
				uint8_t y = gridHeight - (uint8_t)glm::clamp((signed long)(intY[z] * gridHeight / glyphHeight), 0L, (signed long)gridHeight-1) - 1;
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
				uint8_t y1 = gridHeight - (size_t)std::max((signed long)j-1, (signed long)0) - 1;
				uint8_t y2 = gridHeight - (size_t)std::min((signed long)j, (signed long)gridHeight-1) - 1;
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
	
		// Necessary? Should never be required on a closed curve.
		// (Make sure last intersection is >= gridWidth)
		if(*intersections.rbegin() < gridWidth)
			intersections.insert(gridWidth);
		
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
					size_t gridIndex = (gridHeight-i-1)*gridWidth + k;
					grid[gridIndex].insert(254); // Becomes 255 after +1 to remove 0s
				}
			}
			inside = !inside;
			start = end;
		}
	}

	return grid;
}

static void UploadAtlas(GLLabel::AtlasGroup *atlas)
{
	glBindTexture(GL_TEXTURE_2D, atlas->bezierAtlasId);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kBezierAtlasSize, kBezierAtlasSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas->bezierAtlas);
	glBindTexture(GL_TEXTURE_2D, atlas->gridAtlasId);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, kGridAtlasSize, kGridAtlasSize, 0, GL_RGBA, GL_UNSIGNED_BYTE, atlas->gridAtlas);
}

uint32_t GLLabel::LoadCodepointRange(FT_Face face, uint32_t start, uint32_t length)
{
	AtlasGroup *atlas = this->GetOpenAtlasGroup();
	
	for(uint32_t i=start; i<=start+length; ++i)
	{
		// Load the glyph. FT_LOAD_NO_SCALE implies that FreeType should not
		// render the glyph to a bitmap, and ensures that metrics and outline
		// points are represented in font units instead of em.
		FT_UInt glyphIndex = FT_Get_Char_Index(face, i);
		if(FT_Load_Glyph(face, glyphIndex, FT_LOAD_NO_SCALE))
			continue;
		
		FT_Pos glyphWidth = face->glyph->metrics.width;
		FT_Pos glyphHeight = face->glyph->metrics.height;
		uint8_t gridWidth, gridHeight;
		
		std::vector<Bezier> curves = GetCurvesForOutline(&face->glyph->outline);
		std::vector<std::set<uint16_t>> grid = GetGridForCurves(curves, glyphWidth, glyphHeight, gridWidth, gridHeight);
		
		if(curves.size() == 0 || grid.size() == 0)
			continue;
		
		// Although the data is represented as a 32bit texture, it's actually
		// two 16bit ints per pixel, each with an x and y coordinate for
		// the bezier. Every six 16bit ints (3 pixels) is a full bezier
		// Plus two pixels for grid position information
		uint16_t bezierPixelLength = 2 + curves.size()*3;
		
		if(bezierPixelLength > kBezierAtlasSize)
		{
			printf("WARNING: Glyph %i has too many curves. Skipping.\n", i);
			continue;
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
				UploadAtlas(atlas);
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
				UploadAtlas(atlas);
				atlas = this->GetOpenAtlasGroup(); // Should only ever happen once per glyph
			}
		}
		
		uint8_t *bezierData = atlas->bezierAtlas + ((atlas->nextBezierPos[1]+(i-start))*kBezierAtlasSize + atlas->nextGridPos[0])*kAtlasChannels;
		uint16_t *bezierData16 = (uint16_t *)bezierData;

		// TODO: The shader combines each set of bytes into 16 bit ints, which
		// depends on endianness. So this currently only works on little-endian
		for(uint32_t j=0;j<curves.size();++j)
		{
			// 3 pixels = 6 uint16s
			// Scale coords from [0,glyphSize] to [0,maxUShort]
			bezierData16[j*6+0] = curves[j].e0.x * 65535 / glyphWidth;
			bezierData16[j*6+1] = curves[j].e0.y * 65535 / glyphHeight;
			bezierData16[j*6+2] = curves[j].c.x * 65535 / glyphWidth;
			bezierData16[j*6+3] = curves[j].c.y * 65535 / glyphHeight;
			bezierData16[j*6+4] = curves[j].e1.x * 65535 / glyphWidth;
			bezierData16[j*6+5] = curves[j].e1.y * 65535 / glyphHeight;
		}
		
		// Copy grid to atlas
		for(uint32_t y=0;y<gridHeight;++y)
		{
			for(uint32_t x=0;x<gridWidth;++x)
			{
				size_t gridIdx = y*gridWidth + x;
				size_t gridmapIdx = ((kGridAtlasSize - (atlas->nextGridPos[1]+y) - 1)*kGridAtlasSize + (atlas->nextGridPos[0]+x))*kAtlasChannels;
				
				size_t j = 0;
				for(auto it=grid[gridIdx].begin(); it!=grid[gridIdx].end(); ++it)
				{
					if(j >= kAtlasChannels) // TODO: More than four beziers per pixel?
					{
						printf("MORE THAN 4 on %i\n", i);
						break;
					}
					atlas->gridAtlas[gridmapIdx+j] = *it + 1;
					j++;
				}
			}
		}
		
		Glyph glyph = {0};
		glyph.bezierAtlasPos[0] = atlas->nextBezierPos[0];
		glyph.bezierAtlasPos[1] = atlas->nextBezierPos[1];
		glyph.atlasIndex = this->atlases.size()-1;
		// glyph.offset = 
		// glyph.kern = 
		this->glyphs[i] = glyph;
		
		atlas->nextBezierPos[0] += bezierPixelLength;
		atlas->nextGridPos[0] += kGridMaxSize;
	}
	
	UploadAtlas(atlas);
}

size_t GLLabel::GetNumLines()
{
	size_t lines = 0;
	for(size_t i=0; i<this->text.size(); ++i)
		if(this->text[i] == '\n')
			lines ++;
	return lines;
}

static char32_t readNextChar(const char **p, size_t *datalen)
{
	// Gets the next Unicode Code Point from a UTF8 encoded string
	// (Basically converts UTF8 to UTF32 one char at a time)
	// http://stackoverflow.com/questions/2948308/how-do-i-read-utf-8-characters-via-a-pointer/2953960#2953960
	
#define IS_IN_RANGE(c, f, l)    (((c) >= (f)) && ((c) <= (l)))
	
	unsigned char c1, c2, *ptr = (unsigned char*)p;
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