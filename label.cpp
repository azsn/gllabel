/*
 * zelbrium <zelbrium@gmail.com>, 2016.
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
 * The only remaining portion of Dobbie's code is the fragment shader.
 * Although the code had no license, comments seemed to imply the code is
 * freely available for use. I have contacted him to ask for a license,
 * but received no response. Therefore, all of this code except the
 * fragment shader is under the Apache License v2.0, while the fragment
 * shader is unlicensed until further notice.
 * (Dobbie, please let me know if you have issues with this.)
 */

#include "label.hpp"
#include <set>
#include <fstream>
#include <glm/gtc/type_ptr.hpp>

#define sq(x) ((x)*(x))

static GLuint loadShaderProgram(const char *vsCodeC, const char *fsCodeC);

std::shared_ptr<GLFontManager> GLFontManager::singleton = nullptr;

namespace {
extern const char *kGlyphVertexShader;
extern const char *kGlyphFragmentShader;
}

static const uint8_t kGridMaxSize = 20;
static const uint16_t kGridAtlasSize = 256; // Fits exactly 1024 8x8 grids
static const uint16_t kBezierAtlasSize = 256; // Fits around 700-1000 glyphs, depending on their curves
static const uint8_t kAtlasChannels = 4; // Must be 4 (RGBA), otherwise code breaks

GLLabel::GLLabel()
: showingCaret(false), caretPosition(0), prevTime(0)
{
	// this->lastColor = {0,0,0,255};
	this->manager = GLFontManager::GetFontManager();
	// this->lastFace = this->manager->GetDefaultFont();
	// this->manager->LoadASCII(this->lastFace);

	glGenBuffers(1, &this->vertBuffer);
	glGenBuffers(1, &this->caretBuffer);
}

GLLabel::~GLLabel()
{
	glDeleteBuffers(1, &this->vertBuffer);
	glDeleteBuffers(1, &this->caretBuffer);
}

void GLLabel::InsertText(std::u32string text, size_t index, float size, glm::vec4 color, FT_Face face)
{
	if (index > this->text.size()) {
		index = this->text.size();
	}

	this->text.insert(index, text);
	this->glyphs.insert(this->glyphs.begin() + index, text.size(), nullptr);

	size_t prevCapacity = this->verts.capacity();
	GlyphVertex emptyVert = {glm::vec2(), {0}};
	this->verts.insert(this->verts.begin() + index*6, text.size()*6, emptyVert);

	glm::vec2 appendOffset(0, 0);
	if (index > 0) {
		appendOffset = this->verts[(index-1)*6].pos;
		if (this->glyphs[index-1]) {
			appendOffset += -glm::vec2(this->glyphs[index-1]->offset[0], this->glyphs[index-1]->offset[1]) + glm::vec2(this->glyphs[index-1]->advance, 0);
		}
	}
	glm::vec2 initialAppendOffset = appendOffset;

	for (size_t i = 0; i < text.size(); i++) {
		if (text[i] == '\r') {
			this->verts[(index + i)*6].pos = appendOffset;
			continue;
		} else if (text[i] == '\n') {
			appendOffset.x = 0;
			appendOffset.y -= face->height;
			this->verts[(index + i)*6].pos = appendOffset;
			continue;
		} else if (text[i] == '\t') {
			appendOffset.x += 2000;
			this->verts[(index + i)*6].pos = appendOffset;
			continue;
		}

		GLFontManager::Glyph *glyph = this->manager->GetGlyphForCodepoint(face, text[i]);
		if (!glyph) {
			this->verts[(index + i)*6].pos = appendOffset;
			continue;
		}

		GlyphVertex v[6]; // Insertion code depends on v[0] equaling appendOffset (therefore it is also set before continue;s above)
		v[0].pos = glm::vec2(0, 0);
		v[1].pos = glm::vec2(glyph->size[0], 0);
		v[2].pos = glm::vec2(0, glyph->size[1]);
		v[3].pos = glm::vec2(glyph->size[0], glyph->size[1]);
		v[4].pos = glm::vec2(0, glyph->size[1]);
		v[5].pos = glm::vec2(glyph->size[0], 0);
		for (unsigned int j = 0; j < 6; j++) {
			v[j].pos += appendOffset;
			v[j].pos[0] += glyph->offset[0];
			v[j].pos[1] += glyph->offset[1];

			v[j].color = {(uint8_t)(color.r*255), (uint8_t)(color.g*255), (uint8_t)(color.b*255), (uint8_t)(color.a*255)};

			// Encode both the bezier position and the norm coord into one int
			// This theoretically could overflow, but the atlas position will
			// never be over half the size of a uint16, so it's fine.
			unsigned int k = (j < 4) ? j : 6 - j;
			v[j].data[0] = glyph->bezierAtlasPos[0]*2 + ((k & 1) ? 1 : 0);
			v[j].data[1] = glyph->bezierAtlasPos[1]*2 + ((k > 1) ? 1 : 0);
			this->verts[(index + i)*6 + j] = v[j];
		}

		appendOffset.x += glyph->advance;
		this->glyphs[index + i] = glyph;
	}

	// Shift everything after, if necessary
	glm::vec2 deltaAppend = appendOffset - initialAppendOffset;
	for (size_t i = index+text.size(); i < this->text.size(); i++) {
		// If a newline is reached and no change in the y has happened, all
		// glyphs which need to be moved have been moved.
		if (this->text[i] == '\n') {
			if (deltaAppend.y == 0) {
				break;
			}
			if (deltaAppend.x < 0) {
				deltaAppend.x = 0;
			}
		}

		for (unsigned int j = 0; j < 6; j++) {
			this->verts[i*6 + j].pos += deltaAppend;
		}
	}

	glBindBuffer(GL_ARRAY_BUFFER, this->vertBuffer);

	if (this->verts.capacity() != prevCapacity) {
		// If the capacity changed, completely reupload the buffer
		glBufferData(GL_ARRAY_BUFFER, this->verts.capacity() * sizeof(GlyphVertex), NULL, GL_DYNAMIC_DRAW);
		glBufferSubData(GL_ARRAY_BUFFER, 0, this->verts.size() * sizeof(GlyphVertex), &this->verts[0]);
	} else {
		// Otherwise only upload the changed parts
		glBufferSubData(GL_ARRAY_BUFFER,
			index*6*sizeof(GlyphVertex),
			(this->verts.size() - index*6)*sizeof(GlyphVertex),
			&this->verts[index*6]);
	}
	caretTime = 0;
}

void GLLabel::RemoveText(size_t index, size_t length)
{
	if (index >= this->text.size()) {
		return;
	}
	if (index + length > this->text.size()) {
		length = this->text.size() - index;
	}

	glm::vec2 startOffset(0, 0);
	if (index > 0) {
		startOffset = this->verts[(index-1)*6].pos;
		if (this->glyphs[index-1]) {
			startOffset += -glm::vec2(this->glyphs[index-1]->offset[0], this->glyphs[index-1]->offset[1]) + glm::vec2(this->glyphs[index-1]->advance, 0);
		}
	}

	// printf("start offset: %f, %f\n", startOffset.x, startOffset.y);

	// Since all the glyphs between index-1 and index+length have been erased,
	// the end offset will be at index until it gets shifted back
	glm::vec2 endOffset(0, 0);
	// if (this->glyphs[index+length-1])
	// {
		endOffset = this->verts[index*6].pos;
		if (this->glyphs[index+length-1]) {
			endOffset += -glm::vec2(this->glyphs[index+length-1]->offset[0], this->glyphs[index+length-1]->offset[1]) + glm::vec2(this->glyphs[index+length-1]->advance, 0);
		}
	// }

	// printf("end offset: %f, %f\n", endOffset.x, endOffset.y);


	this->text.erase(index, length);
	this->glyphs.erase(this->glyphs.begin() + index, this->glyphs.begin() + (index+length));
	this->verts.erase(this->verts.begin() + index*6, this->verts.begin() + (index+length)*6);

	glm::vec2 deltaOffset = endOffset - startOffset;
	// printf("%f, %f\n", deltaOffset.x, deltaOffset.y);
	// Shift everything after, if necessary
	for (size_t i = index; i < this->text.size(); i++) {
		if (this->text[i] == '\n') {
			deltaOffset.x = 0;
		}

		for (unsigned int j = 0; j < 6; j++) {
			this->verts[i*6 + j].pos -= deltaOffset;
		}
	}

	glBindBuffer(GL_ARRAY_BUFFER, this->vertBuffer);
	//if (this->verts.size() > 0) {
		glBufferSubData(GL_ARRAY_BUFFER,
			index*6*sizeof(GlyphVertex),
			(this->verts.size() - index*6)*sizeof(GlyphVertex),
			&this->verts[index*6]);
	//}

	caretTime = 0;
}

void GLLabel::Render(float time, glm::mat4 transform)
{
	float deltaTime = time - prevTime;
	this->caretTime += deltaTime;

	this->manager->UseGlyphShader();
	this->manager->UploadAtlases();
	this->manager->UseAtlasTextures(0); // TODO: Textures based on each glyph
	this->manager->SetShaderTransform(transform);

	glEnable(GL_BLEND);
	glBindBuffer(GL_ARRAY_BUFFER, this->vertBuffer);
	glEnableVertexAttribArray(0);
	glEnableVertexAttribArray(1);
	glEnableVertexAttribArray(2);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GLLabel::GlyphVertex), (void*)offsetof(GLLabel::GlyphVertex, pos));
	glVertexAttribPointer(1, 2, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(GLLabel::GlyphVertex), (void*)offsetof(GLLabel::GlyphVertex, data));
	glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GLLabel::GlyphVertex), (void*)offsetof(GLLabel::GlyphVertex, color));

	glDrawArrays(GL_TRIANGLES, 0, this->verts.size());

	if (this->showingCaret && !(((int)(this->caretTime*3/2)) % 2)) {
		GLFontManager::Glyph *pipe = this->manager->GetGlyphForCodepoint(this->manager->GetDefaultFont(), '|');

		size_t index = this->caretPosition;

		glm::vec2 offset(0, 0);
		if (index > 0) {
			offset = this->verts[(index-1)*6].pos;
			if (this->glyphs[index-1]) {
				offset += -glm::vec2(this->glyphs[index-1]->offset[0], this->glyphs[index-1]->offset[1]) + glm::vec2(this->glyphs[index-1]->advance, 0);
			}
		}

		GlyphVertex x[6];
		x[0].pos = glm::vec2(0, 0);
		x[1].pos = glm::vec2(pipe->size[0], 0);
		x[2].pos = glm::vec2(0, pipe->size[1]);
		x[3].pos = glm::vec2(pipe->size[0], pipe->size[1]);
		x[4].pos = glm::vec2(0, pipe->size[1]);
		x[5].pos = glm::vec2(pipe->size[0], 0);
		for (unsigned int j = 0; j < 6; j++) {
			x[j].pos += offset;
			x[j].pos[0] += pipe->offset[0];
			x[j].pos[1] += pipe->offset[1];

			x[j].color = {0,0,255,100};

			// Encode both the bezier position and the norm coord into one int
			// This theoretically could overflow, but the atlas position will
			// never be over half the size of a uint16, so it's fine.
			unsigned int k = (j < 4) ? j : 6 - j;
			x[j].data[0] = pipe->bezierAtlasPos[0]*2 + ((k & 1) ? 1 : 0);
			x[j].data[1] = pipe->bezierAtlasPos[1]*2 + ((k > 1) ? 1 : 0);
			// this->verts[(index + i)*6 + j] = v[j];
		}

		glBindBuffer(GL_ARRAY_BUFFER, this->caretBuffer);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GLLabel::GlyphVertex), (void*)offsetof(GLLabel::GlyphVertex, pos));
		glVertexAttribPointer(1, 2, GL_UNSIGNED_SHORT, GL_FALSE, sizeof(GLLabel::GlyphVertex), (void*)offsetof(GLLabel::GlyphVertex, data));
		glVertexAttribPointer(2, 4, GL_UNSIGNED_BYTE, GL_TRUE, sizeof(GLLabel::GlyphVertex), (void*)offsetof(GLLabel::GlyphVertex, color));

		glBufferData(GL_ARRAY_BUFFER, 6 * sizeof(GlyphVertex), &x[0], GL_STREAM_DRAW);
		glDrawArrays(GL_TRIANGLES, 0, 6);
	}

	glDisableVertexAttribArray(0);
	glDisableVertexAttribArray(1);
	glDisableVertexAttribArray(2);
	glDisable(GL_BLEND);
	prevTime = time;
}


GLFontManager::GLFontManager() : defaultFace(nullptr)
{
	if (FT_Init_FreeType(&this->ft) != FT_Err_Ok) {
		printf("Failed to load freetype\n");
	}

	this->glyphShader = loadShaderProgram(kGlyphVertexShader, kGlyphFragmentShader);
	this->uGridAtlas = glGetUniformLocation(glyphShader, "uGridAtlas");
	this->uBezierAtlas = glGetUniformLocation(glyphShader, "uBezierAtlas");
	this->uGridTexel = glGetUniformLocation(glyphShader, "uGridTexel");
	this->uBezierTexel = glGetUniformLocation(glyphShader, "uBezierTexel");
	this->uTransform = glGetUniformLocation(glyphShader, "uTransform");

	this->UseGlyphShader();
	glUniform1i(this->uGridAtlas, 0);
	glUniform1i(this->uBezierAtlas, 1);
	glUniform2f(this->uGridTexel, 1.0/kGridAtlasSize, 1.0/kGridAtlasSize);
	glUniform2f(this->uBezierTexel, 1.0/kBezierAtlasSize, 1.0/kBezierAtlasSize);

	glm::mat4 iden = glm::mat4(1.0);
	glUniformMatrix4fv(this->uTransform, 1, GL_FALSE, glm::value_ptr(iden));
}

GLFontManager::~GLFontManager()
{
	// TODO: Destroy atlases
	glDeleteProgram(this->glyphShader);
	FT_Done_FreeType(this->ft);
}

std::shared_ptr<GLFontManager> GLFontManager::GetFontManager()
{
	if (!GLFontManager::singleton) {
		GLFontManager::singleton = std::shared_ptr<GLFontManager>(new GLFontManager());
	}
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
	std::string path = fontName; // TODO
	return GLFontManager::GetFontFromPath(path);
}

FT_Face GLFontManager::GetDefaultFont()
{
	// TODO
	if (!defaultFace) {
		defaultFace = GLFontManager::GetFontFromPath("fonts/LiberationSans-Regular.ttf");
	}
	return defaultFace;
}

GLFontManager::AtlasGroup * GLFontManager::GetOpenAtlasGroup()
{
	if (this->atlases.size() == 0 || this->atlases[this->atlases.size()-1].full) {
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
	if (almostEqual(a, 0)) {
		float t = (2*B.y - C.y - Y) / (2*(B.y-C.y));
		if (T_VALID(t)) {
			(*outX)[i++] = X_FROM_T(t);
		}
		return i;
	}

	float sqrtTerm = sqrt(Y*a + B.y*B.y - A.y*C.y);

	float t = (A.y - B.y + sqrtTerm) / a;
	if (T_VALID(t)) {
		(*outX)[i++] = X_FROM_T(t);
	}

	t = (A.y - B.y - sqrtTerm) / a;
	if (T_VALID(t)) {
		(*outX)[i++] = X_FROM_T(t);
	}

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

	if (outline->n_points <= 0) {
		return curves;
	}

	// For some reason, the glyphs aren't always positioned with their bottom
	// left corner at 0,0. So find the min x and y values.
	FT_Pos metricsX=outline->points[0].x, metricsY=outline->points[0].y;
	for (short i = 1; i < outline->n_points; i++) {
		metricsX = std::min(metricsX, outline->points[i].x);
		metricsY = std::min(metricsY, outline->points[i].y);
	}

	OutlineDecomposeState state = {{0}};
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

	if (FT_Outline_Decompose(outline, &funcs, &state) == 0) {
		return curves;
	}
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
	for (uint32_t i = 0; i < curves.size(); i++) {
		// TODO: The std::set insert operation is really slow?
		// It appears that this operation nearly doubles the runtime
		// of calculateGridForGlyph.
		#define SETGRID(x, y) { grid[(y)*gridWidth+(x)].insert(i); }

		// If a curve intersects no grid lines, it won't be included. So
		// make sure the cell the the curve starts in is included
		SETGRID(std::min((unsigned long)(curves[i].e0.x * gridWidth / glyphWidth), (unsigned long)gridWidth-1), std::min((unsigned long)(curves[i].e0.y * gridHeight / glyphHeight), (unsigned long)gridHeight-1));

		for (size_t j = 0; j <= gridWidth; j++) {
			glm::vec2 intY(0, 0);
			int num = bezierIntersectVert(&curves[i], &intY, j * glyphWidth / gridWidth);

			for (int z = 0; z < num; z++) {
				uint8_t y = (uint8_t)glm::clamp((signed long)(intY[z] * gridHeight / glyphHeight), 0L, (signed long)gridHeight-1);
				uint8_t x1 = (size_t)std::max((signed long)j-1, (signed long)0);
				uint8_t x2 = (size_t)std::min((signed long)j, (signed long)gridWidth-1);
				SETGRID(x1, y);
				SETGRID(x2, y);
			}
		}

		for (size_t j = 0; j <= gridHeight; j++) {
			glm::vec2 intX(0, 0);
			int num = bezierIntersectHorz(&curves[i], &intX, j * glyphHeight / gridHeight);

			for (int z = 0; z < num; z++) {
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
	for (size_t i = 0; i < gridHeight; i++) {
		intersections.clear();

		float Y = i + 0.5; // Test midpoints of cells
		for (size_t j = 0; j < curves.size(); j++) {
			glm::vec2 intX(0, 0);
			int num = bezierIntersectHorz(&curves[j], &intX, Y * glyphHeight / gridHeight);
			for (int z = 0; z < num; z++) {
				intersections.insert(intX[z] * gridWidth / glyphWidth);
			}
		}

		// TODO: Necessary? Should never be required on a closed curve.
		// (Make sure last intersection is >= gridWidth)
		//if (*intersections.rbegin() < gridWidth) {
		//	intersections.insert(gridWidth);
		//}

		bool inside = false;
		float start = 0;
		for (auto it = intersections.begin(); it != intersections.end(); it++) {
			float end = *it;
			// printf("row %i intersection [%f, %f]\n", i, start, end);
			if (inside) {
				size_t roundS = glm::clamp((float)round(start), (float)0.0, (float)(gridWidth));
				size_t roundE = glm::clamp((float)round(end), (float)0.0, (float)(gridWidth));
				// printf("inside, %i, %i\n", roundS, roundE);

				for (size_t k = roundS; k < roundE; k++) {
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


#pragma pack(push, 1)
struct bitmapdata
{
	char magic[2];
	uint32_t size;
	uint16_t res1;
	uint16_t res2;
	uint32_t offset;

	uint32_t biSize;
	uint32_t width;
	uint32_t height;
	uint16_t planes;
	uint16_t bitCount;
	uint32_t compression;
	uint32_t imageSizeBytes;
	uint32_t xpelsPerMeter;
	uint32_t ypelsPerMeter;
	uint32_t clrUsed;
	uint32_t clrImportant;
};
#pragma pack(pop)

void writeBMP(const char *path, uint32_t width, uint32_t height, uint16_t channels, uint8_t *data)
{
	FILE *f = fopen(path, "wb");

	bitmapdata head;
	head.magic[0] = 'B';
	head.magic[1] = 'M';
	head.size = sizeof(bitmapdata) + width*height*channels;
	head.res1 = 0;
	head.res2 = 0;
	head.offset = sizeof(bitmapdata);
	head.biSize = 40;
	head.width = width;
	head.height = height;
	head.planes = 1;
	head.bitCount = 8*channels;
	head.compression = 0;
	head.imageSizeBytes = width*height*channels;
	head.xpelsPerMeter = 0;
	head.ypelsPerMeter = 0;
	head.clrUsed = 0;
	head.clrImportant = 0;

	fwrite(&head, sizeof(head), 1, f);
	fwrite(data, head.imageSizeBytes, 1, f);
	fclose(f);
}


GLFontManager::Glyph * GLFontManager::GetGlyphForCodepoint(FT_Face face, uint32_t point)
{
	auto faceIt = this->glyphs.find(face);
	if (faceIt != this->glyphs.end()) {
		auto glyphIt = faceIt->second.find(point);
		if (glyphIt != faceIt->second.end()) {
			return &glyphIt->second;
		}
	}

	AtlasGroup *atlas = this->GetOpenAtlasGroup();

	// Load the glyph. FT_LOAD_NO_SCALE implies that FreeType should not
	// render the glyph to a bitmap, and ensures that metrics and outline
	// points are represented in font units instead of em.
	FT_UInt glyphIndex = FT_Get_Char_Index(face, point);
	if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_NO_SCALE)) {
		return nullptr;
	}

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

	if (curves.size() == 0 || grid.size() == 0 || bezierPixelLength > kBezierAtlasSize) {
		if (bezierPixelLength > kBezierAtlasSize) {
			printf("WARNING: Glyph %i has too many curves\n", point);
		}

		GLFontManager::Glyph glyph = {{0}};
		glyph.bezierAtlasPos[2] = -1;
		glyph.size[0] = glyphWidth;
		glyph.size[1] = glyphHeight;
		glyph.offset[0] = face->glyph->metrics.horiBearingX;
		glyph.offset[1] = face->glyph->metrics.horiBearingY - glyphHeight;
		glyph.advance = face->glyph->metrics.horiAdvance;
		this->glyphs[face][point] = glyph;
		return &this->glyphs[face][point];
	}

	// Find an open position in the bezier atlas
	if (atlas->nextBezierPos[0] + bezierPixelLength > kBezierAtlasSize) {
		// Next row
		atlas->nextBezierPos[1] ++;
		atlas->nextBezierPos[0] = 0;
		if (atlas->nextBezierPos[1] >= kBezierAtlasSize) {
			atlas->full = true;
			atlas->uploaded = false;
			atlas = this->GetOpenAtlasGroup();
		}
	}

	// Find an open position in the grid atlas
	if (atlas->nextGridPos[0] + kGridMaxSize > kGridAtlasSize) {
		atlas->nextGridPos[1] += kGridMaxSize;
		atlas->nextGridPos[0] = 0;
		if (atlas->nextGridPos[1] >= kGridAtlasSize) {
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
	for (uint32_t j = 0; j < curves.size(); j++) {
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
	for (uint32_t y = 0; y < gridHeight; y++) {
		for (uint32_t x = 0; x < gridWidth; x++) {
			size_t gridIdx = y*gridWidth + x;
			size_t gridmapIdx = ((atlas->nextGridPos[1]+y)*kGridAtlasSize + (atlas->nextGridPos[0]+x))*kAtlasChannels;

			size_t j = 0;
			for (auto it = grid[gridIdx].begin(); it != grid[gridIdx].end(); it++) {
				if (j >= kAtlasChannels) { // TODO: More than four beziers per pixel?
					printf("MORE THAN 4 on %i\n", point);
					break;
				}
				atlas->gridAtlas[gridmapIdx+j] = *it + 1;
				j++;
			}
		}
	}

	GLFontManager::Glyph glyph = {{0}};
	glyph.bezierAtlasPos[0] = atlas->nextBezierPos[0];
	glyph.bezierAtlasPos[1] = atlas->nextBezierPos[1];
	glyph.bezierAtlasPos[2] = this->atlases.size()-1;
	glyph.size[0] = glyphWidth;
	glyph.size[1] = glyphHeight;
	glyph.offset[0] = face->glyph->metrics.horiBearingX;
	glyph.offset[1] = face->glyph->metrics.horiBearingY - glyphHeight;
	glyph.advance = face->glyph->metrics.horiAdvance;
	this->glyphs[face][point] = glyph;

	atlas->nextBezierPos[0] += bezierPixelLength;
	atlas->nextGridPos[0] += kGridMaxSize;
	atlas->uploaded = false;

	writeBMP("bezierAtlas.bmp", kBezierAtlasSize, kBezierAtlasSize, 4, atlas->bezierAtlas);
	writeBMP("gridAtlas.bmp", kGridAtlasSize, kGridAtlasSize, 4, atlas->gridAtlas);

	return &this->glyphs[face][point];
}

void GLFontManager::LoadASCII(FT_Face face)
{
	if (!face) {
		return;
	}

	this->GetGlyphForCodepoint(face, 0);

	for (int i = 32; i < 128; i++) {
		this->GetGlyphForCodepoint(face, i);
	}
}

void GLFontManager::UploadAtlases()
{
	for (size_t i = 0; i < this->atlases.size(); i++) {
		if (this->atlases[i].uploaded) {
			continue;
		}

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

void GLFontManager::SetShaderTransform(glm::mat4 transform)
{
	glUniformMatrix4fv(this->uTransform, 1, GL_FALSE, glm::value_ptr(transform));
}

void GLFontManager::UseAtlasTextures(uint16_t atlasIndex)
{
	if (atlasIndex >= this->atlases.size()) {
		return;
	}

	glActiveTexture(GL_TEXTURE0);
	glBindTexture(GL_TEXTURE_2D, this->atlases[atlasIndex].gridAtlasId);
	glActiveTexture(GL_TEXTURE1);
	glBindTexture(GL_TEXTURE_2D, this->atlases[atlasIndex].bezierAtlasId);
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
	if (infoLogLength > 1) {
		std::vector<char> infoLog(infoLogLength+1);
		glGetShaderInfoLog(vertexShaderId, infoLogLength, NULL, &infoLog[0]);
		printf("[Vertex] %s\n", &infoLog[0]);
	}
	if (!result) {
		return 0;
	}

	// Compile fragment shader
	GLuint fragmentShaderId = glCreateShader(GL_FRAGMENT_SHADER);
	glShaderSource(fragmentShaderId, 1, &fsCodeC, NULL);
	glCompileShader(fragmentShaderId);

	result = GL_FALSE, infoLogLength = 0;
	glGetShaderiv(fragmentShaderId, GL_COMPILE_STATUS, &result);
	glGetShaderiv(fragmentShaderId, GL_INFO_LOG_LENGTH, &infoLogLength);
	if (infoLogLength > 1) {
		std::vector<char> infoLog(infoLogLength);
		glGetShaderInfoLog(fragmentShaderId, infoLogLength, NULL, &infoLog[0]);
		printf("[Fragment] %s\n", &infoLog[0]);
	}
	if (!result) {
		return 0;
	}

	// Link the program
	GLuint programId = glCreateProgram();
	glAttachShader(programId, vertexShaderId);
	glAttachShader(programId, fragmentShaderId);
	glLinkProgram(programId);

	result = GL_FALSE, infoLogLength = 0;
	glGetProgramiv(programId, GL_LINK_STATUS, &result);
	glGetProgramiv(programId, GL_INFO_LOG_LENGTH, &infoLogLength);
	if (infoLogLength > 1) {
		std::vector<char> infoLog(infoLogLength+1);
		glGetProgramInfoLog(programId, infoLogLength, NULL, &infoLog[0]);
		printf("[Shader Linker] %s\n", &infoLog[0]);
	}
	if (!result) {
		return 0;
	}

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
uniform mat4 uTransform;

layout(location = 0) in vec2 vPosition;
layout(location = 1) in vec2 vData;
layout(location = 2) in vec4 vColor;

out vec4 oColor;
out vec2 oBezierCoord;
out vec2 oNormCoord;
out vec4 oGridRect;

float ushortFromVec2(vec2 v)
{
	return (v.y * 65280.0 + v.x * 255.0);
}

vec2 vec2FromPixel(vec2 coord)
{
	vec4 pixel = texture(uBezierAtlas, (coord+0.5)*uBezierTexel);
	return vec2(ushortFromVec2(pixel.xy), ushortFromVec2(pixel.zw));
}

void main()
{
	oColor = vColor;
	oBezierCoord = floor(vData * 0.5);
	oNormCoord = mod(vData, 2.0);
	oGridRect = vec4(vec2FromPixel(oBezierCoord), vec2FromPixel(oBezierCoord + vec2(1,0)));
	gl_Position = uTransform*vec4(vPosition, 0.0, 1.0);
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

in vec4 oColor;
in vec2 oBezierCoord;
in vec2 oNormCoord;
in vec4 oGridRect;

layout(location = 0) out vec4 outColor;

float positionAt(float p0, float p1, float p2, float t)
{
	float mt = 1.0 - t;
	return mt*mt*p0 + 2.0*t*mt*p1 + t*t*p2;
}

float tangentAt(float p0, float p1, float p2, float t)
{
	return 2.0 * (1.0-t) * (p1 - p0) + 2.0 * t * (p2 - p1);
}

bool almostEqual(float a, float b)
{
	return abs(a-b) < 1e-5;
}

float normalizedUshortFromVec2(vec2 v)
{
	return (v.y * 65280.0 + v.x * 255.0) / 65536.0;
}

vec4 getPixelByXY(vec2 coord)
{
	return texture(uBezierAtlas, (coord+0.5)*uBezierTexel);
}

void fetchBezier(int coordIndex, out vec2 p[3])
{
	for (int i=0; i<3; i++) {
		vec4 pixel = getPixelByXY(vec2(oBezierCoord.x + 2 + coordIndex*3 + i, oBezierCoord.y));
		p[i] = vec2(normalizedUshortFromVec2(pixel.xy), normalizedUshortFromVec2(pixel.zw)) - oNormCoord;
	}
}

int getAxisIntersections(float p0, float p1, float p2, out vec2 t)
{
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

float integrateWindow(float x)
{
	float xsq = x*x;
	return sign(x) * (0.5 * xsq*xsq - xsq) + 0.5;  // parabolic window
	//return 0.5 * (1.0 - sign(x) * xsq);          // box window
}

mat2 getUnitLineMatrix(vec2 b1, vec2 b2)
{
	vec2 V = b2 - b1;
	float normV = length(V);
	V = V / (normV*normV);

	return mat2(V.x, -V.y, V.y, V.x);
}

void updateClosestCrossing(in vec2 porig[3], mat2 M, inout float closest, vec2 integerCell)
{
	vec2 p[3];
	for (int i=0; i<3; i++) {
		p[i] = M * porig[i];
	}

	vec2 t;
	int numT = getAxisIntersections(p[0].y, p[1].y, p[2].y, t);

	for (int i=0; i<2; i++) {
		if (i == numT) {
			break;
		}

		if (t[i] > 0.0 && t[i] < 1.0) {
			float posx = positionAt(p[0].x, p[1].x, p[2].x, t[i]);
			vec2 op = vec2(positionAt(porig[0].x, porig[1].x, porig[2].x, t[i]),
			               positionAt(porig[0].y, porig[1].y, porig[2].y, t[i]));
			op += oNormCoord;

			bool sameCell = floor(clamp(op * oGridRect.zw, vec2(0.5), vec2(oGridRect.zw)-0.5)) == integerCell;

			//if (posx > 0.0 && posx < 1.0 && posx < abs(closest)) {
			if (sameCell && abs(posx) < abs(closest)) {
				float derivy = tangentAt(p[0].y, p[1].y, p[2].y, t[i]);
				closest = (derivy < 0.0) ? -posx : posx;
			}
		}
	}
}

mat2 inverse(mat2 m)
{
	return mat2(m[1][1],-m[0][1], -m[1][0], m[0][0])
		/ (m[0][0]*m[1][1] - m[0][1]*m[1][0]);
}

void main()
{
	vec2 integerCell = floor(clamp(oNormCoord * oGridRect.zw, vec2(0.5), vec2(oGridRect.zw)-0.5));
	vec2 indicesCoord = oGridRect.xy + integerCell + 0.5;
	vec2 cellMid = (integerCell + 0.5) / oGridRect.zw;

	mat2 initrot = inverse(mat2(dFdx(oNormCoord) * kPixelWindowSize, dFdy(oNormCoord) * kPixelWindowSize));

	float theta = pi/float(numSS);
	mat2 rotM = mat2(cos(theta), sin(theta), -sin(theta), cos(theta)); // note this is column major ordering

	ivec4 indices1;
	indices1 = ivec4(texture(uGridAtlas, indicesCoord*uGridTexel) * 255.0 + 0.5);
	// indices2 = ivec4(texture(uAtlasSampler, vec2(indicesCoord.x + vGridSize.x, indicesCoord.y) * uTexelSize) * 255.0 + 0.5);

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

		//if (bezierIndex < 4) {
			coordIndex = indices1[bezierIndex];
		//} else {
		//	 if (!moreThanFourIndices) break;
		//	 coordIndex = indices2[bezierIndex-4];
		//}

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
		} // ss
	}

	bool midVal = midClosest < 0.0;

	// Add contribution from rays that started inside
	for (int ss=0; ss<numSS; ss++) {
		if ((firstIntersection[ss] >= 2.0 && midVal) || (firstIntersection[ss] > 0.0 && abs(firstIntersection[ss]) < 2.0)) {
			percent = percent + 1.0 /*integrateWindow(-1.0)*/;
		}
	}

	percent = percent / float(numSS);
	outColor = oColor;
	outColor.a *= percent;
}
)";
}
