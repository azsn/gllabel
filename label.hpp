#include <string>
#include <vector>
#include <memory>
#include <map>
#include <glew.h>
#include <glm/glm.hpp>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

class GLFontManager
{
public:
	struct AtlasGroup
	{
		// Grid atlas contains an array of square grids with side length
		// gridMaxSize. Each grid takes a single glyph and splits it into
		// cells that inform the fragment shader which curves of the glyph
		// intersect that cell. The cell contains coords to data in the bezier
		// atlas. The bezier atlas contains the actual bezier curves for each
		// glyph. All data for a single glyph must lie in a single row, although
		// multiple glyphs can be in one row. Each bezier curve takes three
		// "RGBA pixels" (12 bytes) of data.
		// Both atlases also encode some extra information, which is explained
		// where it is used in the code.
		GLuint bezierAtlasId, gridAtlasId;
		uint8_t *bezierAtlas, *gridAtlas;
		uint16_t nextBezierPos[2], nextGridPos[2]; // XY pixel coordinates 
		bool full; // For faster checking
		bool uploaded;
	};
	
	struct Glyph
	{
		uint16_t size[2]; // Width and height in FT units
		int16_t offset[2]; // Offset of glyph in FT units
		uint16_t bezierAtlasPos[3]; // XYZ pixel coordinates (Z being atlas index)
		int16_t advance; // Amount to advance after character in FT units
	};

public: // TODO: private
	std::vector<AtlasGroup> atlases;
	std::map<FT_Face, std::map<uint32_t, Glyph>> glyphs;
	FT_Library ft;
	GLuint glyphShader, uGridAtlas, uBezierAtlas, uGridTexel, uBezierTexel, uTransform;
	
	GLFontManager();
	
	AtlasGroup * GetOpenAtlasGroup();
	
public:
	~GLFontManager();

	static std::shared_ptr<GLFontManager> singleton;
	static std::shared_ptr<GLFontManager> GetFontManager();	
	
	FT_Face GetFontFromPath(std::string fontPath);
	FT_Face GetFontFromName(std::string fontName);
	FT_Face GetDefaultFont();
	
	Glyph * GetGlyphForCodepoint(FT_Face face, uint32_t point);
	void LoadASCII(FT_Face face);
	void UploadAtlases();
	
	void UseGlyphShader();
	void SetShaderTransform(glm::mat4 transform);
	void UseAtlasTextures(uint16_t atlasIndex);
};

class GLLabel
{
public:
	enum class Align
	{
		Start,
		Center,
		End
	};
	
	struct Color
	{
		uint8_t r,g,b,a;
	};
	
private:
	struct GlyphVertex
	{
		// XY coords of the vertex
		glm::vec2 pos;
		
		// The UV coords of the data for this glyph in the bezier atlas
		// Also contains a vertex-dependent norm coordiate by encoding:
		// encode: data[n] = coord[n] * 2 + norm[n]
		// decode: norm[n] = data[n] % 2,  coord[n] = int(data[n] / 2)
		uint16_t data[2];
		
		// RGBA color [0,255]
		Color color;
	};
	
	std::shared_ptr<GLFontManager> manager;
	std::vector<GlyphVertex> verts;
	GLuint vertBuffer;
	
	std::u32string text;
	glm::vec2 appendOffset;
	Align horzAlign, vertAlign;
	FT_Face lastFace;
	Color lastColor;
	bool showingCaret;
		
public:
	GLLabel();
	GLLabel(std::u32string text);
	~GLLabel();
	
	inline std::u32string GetText() { return this->text; }

	inline void SetText(std::u32string text) { SetText(text, lastFace, lastColor); }
	inline void SetText(std::u32string text, std::string face, Color color) { SetText(text, manager->GetFontFromName(face), color); }
	void SetText(std::u32string text, FT_Face face, Color color);
	
	inline void AppendText(std::u32string text) { AppendText(text, lastFace, lastColor); }
	inline void AppendText(std::u32string text, std::string face, Color color) { SetText(text, manager->GetFontFromName(face), color); }
	void AppendText(std::u32string text, FT_Face face, Color color);

	void SetHorzAlignment(Align horzAlign);
	void SetVertAlignment(Align vertAlign);
	void ShowCaret(bool show) { showingCaret = show; }
	
	// Render the label. Also uploads modified textures as necessary. 'time'
	// should be passed in monotonic seconds (no specific zero time necessary).
	void Render(float time, glm::mat4 transform);
};