/*
 * zelbrium <zelbrium@gmail.com>, 2016-2020.
 *
 * This code is based on Will Dobbie's WebGL vector-based text rendering (2016).
 * It can be found here:
 * https://wdobbie.com/post/gpu-text-rendering-with-vector-textures/
 *
 * Dobbie's original code used a pre-generated bezier curve atlas generated
 * from a PDF. This GLLabel class allows for live text rendering based on
 * glyph curves exported from FreeType2.
 *
 * Text is rendered size-independently. This means you can scale, rotate,
 * or reposition text rendered using GLLabel without any loss of quality.
 * All that's required is a font file to load the text from. Currently, any TTF
 * font that does not use cubic beziers or make use of very detailed glyphs,
 * such as many Hanzi / Kanji characters, should work.
 */

#include <gllabel.hpp>
#include "vgrid.hpp"
#include "cubic2quad.hpp"
#include <set>
#include <fstream>
#include <iostream>
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

void GLLabel::InsertText(std::u32string text, size_t index, glm::vec4 color, FT_Face face)
{
	if (index > this->text.size()) {
		index = this->text.size();
	}

	this->text.insert(index, text);
	this->glyphs.insert(this->glyphs.begin() + index, text.size(), nullptr);

	size_t prevCapacity = this->verts.capacity();
	GlyphVertex emptyVert{};
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

		GlyphVertex v[6]{}; // Insertion code depends on v[0] equaling appendOffset (therefore it is also set before continue;s above)
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
			unsigned int normX = k & 1;
			unsigned int normY = k > 1;
			unsigned int norm = (normX << 1) + normY;
			v[j].data = (glyph->bezierAtlasPos[0] << 2) + norm;
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

	this->text.erase(index, length);
	this->glyphs.erase(this->glyphs.begin() + index, this->glyphs.begin() + (index+length));
	this->verts.erase(this->verts.begin() + index*6, this->verts.begin() + (index+length)*6);

	glm::vec2 deltaOffset = endOffset - startOffset;
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
	if (this->verts.size() > 0) {
		glBufferSubData(GL_ARRAY_BUFFER,
			index*6*sizeof(GlyphVertex),
			(this->verts.size() - index*6)*sizeof(GlyphVertex),
			&this->verts[index*6]);
	}

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
	glVertexAttribPointer(1, 1, GL_UNSIGNED_INT, GL_FALSE, sizeof(GLLabel::GlyphVertex), (void*)offsetof(GLLabel::GlyphVertex, data));
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

		GlyphVertex x[6]{};
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
			unsigned int normX = k & 1;
			unsigned int normY = k > 1;
			unsigned int norm = (normX << 1) + normY;
			x[j].data = (pipe->bezierAtlasPos[0] << 2) + norm;
			// this->verts[(index + i)*6 + j] = v[j];
		}

		glBindBuffer(GL_ARRAY_BUFFER, this->caretBuffer);
		glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(GLLabel::GlyphVertex), (void*)offsetof(GLLabel::GlyphVertex, pos));
		glVertexAttribPointer(1, 1, GL_UNSIGNED_INT, GL_FALSE, sizeof(GLLabel::GlyphVertex), (void*)offsetof(GLLabel::GlyphVertex, data));
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
		std::cerr << "Failed to load freetype\n";
	}

	this->glyphShader = loadShaderProgram(kGlyphVertexShader, kGlyphFragmentShader);
	this->uGridAtlas = glGetUniformLocation(glyphShader, "uGridAtlas");
	this->uGlyphData = glGetUniformLocation(glyphShader, "uGlyphData");
	this->uTransform = glGetUniformLocation(glyphShader, "uTransform");

	this->UseGlyphShader();
	glUniform1i(this->uGridAtlas, 0);
	glUniform1i(this->uGlyphData, 1);

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
		AtlasGroup group{};
		group.glyphDataBuf = new uint8_t[sq(kBezierAtlasSize)*kAtlasChannels]();
		group.gridAtlas = new uint8_t[sq(kGridAtlasSize)*kAtlasChannels]();
		group.uploaded = true;

		// https://www.khronos.org/opengl/wiki/Buffer_Texture
		// TODO: Check GL_MAX_TEXTURE_BUFFER_SIZE
		glGenBuffers(1, &group.glyphDataBufId);
		glBindBuffer(GL_TEXTURE_BUFFER, group.glyphDataBufId);
		glGenTextures(1, &group.glyphDataBufTexId);
		glBindTexture(GL_TEXTURE_BUFFER, group.glyphDataBufTexId);
		glTexBuffer(GL_TEXTURE_BUFFER, GL_RGBA8, group.glyphDataBufId);

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

struct OutlineDecomposeState
{
	FT_Vector prevPoint;
	std::vector<Bezier2> *curves;
	FT_Pos metricsX;
	FT_Pos metricsY;
	double *c2qOut;
	bool clockwise;
};

/*
 * Uses FreeType's outline decomposing to convert an outline into a vector
 * of beziers. This just makes working with the outline easier.
 */
static std::vector<Bezier2> GetCurvesForOutline(FT_Outline *outline)
{
	std::vector<Bezier2> curves;

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

	FT_Orientation orientation = FT_Outline_Get_Orientation(outline);
	bool clockwise = (orientation == FT_ORIENTATION_FILL_RIGHT);

	double c2qOut[C2Q_OUT_LEN];

	OutlineDecomposeState state{};
	state.curves = &curves;
	state.metricsX = metricsX;
	state.metricsY = metricsY;
	state.c2qOut = c2qOut;
	state.clockwise = clockwise;

	FT_Outline_Funcs funcs{};
	funcs.move_to = [](const FT_Vector *to, void *user) -> int {
		auto state = static_cast<OutlineDecomposeState *>(user);
		state->prevPoint = *to;
		return 0;
	};
	funcs.line_to = [](const FT_Vector *to, void *user) -> int {
		auto state = static_cast<OutlineDecomposeState *>(user);
		Vec2 begin = Vec2(state->prevPoint.x - state->metricsX, state->prevPoint.y - state->metricsY);
		Vec2 end = Vec2(to->x - state->metricsX, to->y - state->metricsY);

		Bezier2 b;
		if (state->clockwise) {
			b.e0 = begin;
			b.c = begin;
			b.e1 = end;
		} else {
			b.e0 = end;
			b.c = end;
			b.e1 = begin;
		}

		state->curves->push_back(b);
		state->prevPoint = *to;
		return 0;
	};
	funcs.conic_to = [](const FT_Vector *control, const FT_Vector *to, void *user) -> int {
		auto state = static_cast<OutlineDecomposeState *>(user);
		Vec2 begin = Vec2(state->prevPoint.x - state->metricsX, state->prevPoint.y - state->metricsY);
		Vec2 c = Vec2(control->x - state->metricsX, control->y - state->metricsY);
		Vec2 end = Vec2(to->x - state->metricsX, to->y - state->metricsY);

		Bezier2 b;
		if (state->clockwise) {
			b.e0 = begin;
			b.c = c;
			b.e1 = end;
		} else {
			b.e0 = end;
			b.c = c;
			b.e1 = begin;
		}

		state->curves->push_back(b);
		state->prevPoint = *to;
		return 0;
	};
	funcs.cubic_to = [](const FT_Vector *c1, const FT_Vector *c2, const FT_Vector *to, void *user) -> int {
		auto state = static_cast<OutlineDecomposeState *>(user);
		double in[8] = {
			(double)state->prevPoint.x - state->metricsX, (double)state->prevPoint.y - state->metricsY,
			(double)c1->x - state->metricsX, (double)c1->y - state->metricsY,
			(double)c2->x - state->metricsX, (double)c2->y - state->metricsY,
			(double)to->x - state->metricsX, (double)to->y - state->metricsY,
		};
		double *out = state->c2qOut;
		int nvals = 6 * cubic2quad(in, 5, out);
		for (int i = 0; i < nvals; i += 6) {
			Vec2 begin = Vec2((float)out[i], (float)out[i+1]);
			Vec2 c = Vec2((float)out[i+2], (float)out[i+3]);
			Vec2 end = Vec2((float)out[i+4], (float)out[i+5]);

			Bezier2 b;
			if (state->clockwise) {
				b.e0 = begin;
				b.c = c;
				b.e1 = end;
			} else {
				b.e0 = end;
				b.c = c;
				b.e1 = begin;
			}

			state->curves->push_back(b);
		}
		state->prevPoint = *to;
		return 0;
	};

	if (FT_Outline_Decompose(outline, &funcs, &state) == 0) {
		return curves;
	}
	return std::vector<Bezier2>();
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

// A bezier is written as 6 16-bit integers (12 bytes). Increments buffer by
// the number of bytes written (always 12). Coords are scaled from
// [0,glyphSize] to [0,UINT16_MAX].
void write_bezier_to_buffer(uint16_t **pbuffer, Bezier2 *bezier, Vec2 *glyphSize)
{
	uint16_t *buffer = *pbuffer;
	buffer[0] = bezier->e0.x * UINT16_MAX / glyphSize->w;
	buffer[1] = bezier->e0.y * UINT16_MAX / glyphSize->h;
	buffer[2] = bezier->c.x  * UINT16_MAX / glyphSize->w;
	buffer[3] = bezier->c.y  * UINT16_MAX / glyphSize->h;
	buffer[4] = bezier->e1.x * UINT16_MAX / glyphSize->w;
	buffer[5] = bezier->e1.y * UINT16_MAX / glyphSize->h;
	*pbuffer += 6;
}

void write_glyph_data_to_buffer(
	uint8_t *buffer8,
	std::vector<Bezier2> &beziers,
	Vec2 &glyphSize,
	uint16_t gridX,
	uint16_t gridY,
	uint16_t gridWidth,
	uint16_t gridHeight)
{
	uint16_t *buffer = (uint16_t *)buffer8;
	buffer[0] = gridX;
	buffer[1] = gridY;
	buffer[2] = gridWidth;
	buffer[3] = gridHeight;
	buffer += 4;

	for (size_t i = 0; i < beziers.size(); i++) {
		write_bezier_to_buffer(&buffer, &beziers[i], &glyphSize);
	}
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
	uint8_t gridWidth = kGridMaxSize;
	uint8_t gridHeight = kGridMaxSize;

	std::vector<Bezier2> curves = GetCurvesForOutline(&face->glyph->outline);
	VGrid grid(curves, Vec2(glyphWidth, glyphHeight), gridWidth, gridHeight);

	// Although the data is represented as a 32bit texture, it's actually
	// two 16bit ints per pixel, each with an x and y coordinate for
	// the bezier. Every six 16bit ints (3 pixels) is a full bezier
	// Plus two pixels for grid position information
	uint16_t bezierPixelLength = 2 + curves.size()*3;

	bool tooManyCurves = uint32_t(bezierPixelLength) > sq(uint32_t(kBezierAtlasSize));

	if (curves.size() == 0 || tooManyCurves) {
		if (tooManyCurves) {
			std::cerr << "WARN: Glyph " << point << " has too many curves\n";
		}

		GLFontManager::Glyph glyph{};
		glyph.bezierAtlasPos[1] = -1;
		glyph.size[0] = glyphWidth;
		glyph.size[1] = glyphHeight;
		glyph.offset[0] = face->glyph->metrics.horiBearingX;
		glyph.offset[1] = face->glyph->metrics.horiBearingY - glyphHeight;
		glyph.advance = face->glyph->metrics.horiAdvance;
		this->glyphs[face][point] = glyph;
		return &this->glyphs[face][point];
	}

	// Find an open position in the bezier atlas
	if (atlas->glyphDataBufOffset + bezierPixelLength > sq(kBezierAtlasSize)) {
		atlas->full = true;
		atlas->uploaded = false;
		atlas = this->GetOpenAtlasGroup();
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

	uint8_t *bezierData = atlas->glyphDataBuf + (atlas->glyphDataBufOffset * kAtlasChannels);

	Vec2 glyphSize(glyphWidth, glyphHeight);
	write_glyph_data_to_buffer(
		bezierData,
		curves,
		glyphSize,
		atlas->nextGridPos[0],
		atlas->nextGridPos[1],
		kGridMaxSize,
		kGridMaxSize);

	// TODO: Integrate with AtlasGroup / replace AtlasGroup
	VGridAtlas gridAtlas{};
	gridAtlas.data = atlas->gridAtlas;
	gridAtlas.width = kGridAtlasSize;
	gridAtlas.height = kGridAtlasSize;
	gridAtlas.depth = kAtlasChannels;
	gridAtlas.WriteVGridAt(grid, atlas->nextGridPos[0], atlas->nextGridPos[1]);

	GLFontManager::Glyph glyph{};
	glyph.bezierAtlasPos[0] = atlas->glyphDataBufOffset;
	glyph.bezierAtlasPos[1] = this->atlases.size()-1;
	glyph.size[0] = glyphWidth;
	glyph.size[1] = glyphHeight;
	glyph.offset[0] = face->glyph->metrics.horiBearingX;
	glyph.offset[1] = face->glyph->metrics.horiBearingY - glyphHeight;
	glyph.advance = face->glyph->metrics.horiAdvance;
	this->glyphs[face][point] = glyph;

	atlas->glyphDataBufOffset += bezierPixelLength;
	atlas->nextGridPos[0] += kGridMaxSize;
	atlas->uploaded = false;

	writeBMP("bezierAtlas.bmp", kBezierAtlasSize, kBezierAtlasSize, 4, atlas->glyphDataBuf);
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

		glBindBuffer(GL_TEXTURE_BUFFER, this->atlases[i].glyphDataBufId);
		glBufferData(GL_TEXTURE_BUFFER, sq(kBezierAtlasSize)*kAtlasChannels,
			this->atlases[i].glyphDataBuf, GL_STREAM_DRAW);

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
	glBindTexture(GL_TEXTURE_BUFFER, this->atlases[atlasIndex].glyphDataBufTexId);
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
		std::cerr << "[Vertex] " << &infoLog[0] << "\n";
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
		std::cerr << "[Fragment] " << &infoLog[0] << "\n";
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
		std::cerr << "[Shader Linker] " << &infoLog[0] << "\n";
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
uniform samplerBuffer uGlyphData;
uniform mat4 uTransform;

layout(location = 0) in vec2 vPosition;
layout(location = 1) in uint vData;
layout(location = 2) in vec4 vColor;

out vec4 oColor;
flat out uint glyphDataOffset;
flat out ivec4 oGridRect;
out vec2 oNormCoord;

float ushortFromVec2(vec2 v)
{
	return (v.y * 65280.0 + v.x * 255.0);
}

ivec2 vec2FromPixel(uint offset)
{
	vec4 pixel = texelFetch(uGlyphData, int(offset));
	return ivec2(ushortFromVec2(pixel.xy), ushortFromVec2(pixel.zw));
}

void main()
{
	oColor = vColor;
	glyphDataOffset = vData >> 2u;
	oNormCoord = vec2((vData & 2u) >> 1, vData & 1u);
	oGridRect = ivec4(vec2FromPixel(glyphDataOffset), vec2FromPixel(glyphDataOffset + 1u));
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
uniform samplerBuffer uGlyphData;

in vec4 oColor;
flat in uint glyphDataOffset;
flat in ivec4 oGridRect;
in vec2 oNormCoord;

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

vec4 getPixelByOffset(int offset)
{
	return texelFetch(uGlyphData, offset);
}

void fetchBezier(int coordIndex, out vec2 p[3])
{
	for (int i=0; i<3; i++) {
		vec4 pixel = getPixelByOffset(int(glyphDataOffset) + 2 + coordIndex*3 + i);
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

ivec2 normalizedCoordToIntegerCell(vec2 ncoord)
{
	return clamp(ivec2(ncoord * oGridRect.zw), ivec2(0), oGridRect.zw - 1);
}

void updateClosestCrossing(in vec2 porig[3], mat2 M, inout float closest, ivec2 integerCell)
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

			bool sameCell = normalizedCoordToIntegerCell(op) == integerCell;

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
	ivec2 integerCell = normalizedCoordToIntegerCell(oNormCoord);
	ivec2 indicesCoord = ivec2(oGridRect.xy + integerCell);
	vec2 cellMid = (integerCell + 0.5) / oGridRect.zw;

	mat2 initrot = inverse(mat2(dFdx(oNormCoord) * kPixelWindowSize, dFdy(oNormCoord) * kPixelWindowSize));

	float theta = pi/float(numSS);
	mat2 rotM = mat2(cos(theta), sin(theta), -sin(theta), cos(theta)); // note this is column major ordering

	ivec4 indices1 = ivec4(texelFetch(uGridAtlas, indicesCoord, 0) * 255.0);

	// The mid-inside flag is encoded by the order of the beziers indices.
	// See write_vgrid_cell_to_buffer() for details.
	bool midInside = indices1[0] > indices1[1];

	float midClosest = midInside ? -2.0 : 2.0;

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

		// Indices 0 and 1 are both "no bezier" -- see
		// write_vgrid_cell_to_buffer() for why.
		if (coordIndex < 2) {
			continue;
		}

		vec2 p[3];
		fetchBezier(coordIndex-2, p);

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
