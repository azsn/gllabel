#include <string>
#include <vector>
#include <map>
#include <glew.h>
#include <glm/glm.hpp>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

class GLLabel
{
public:
	enum class Align
	{
		Start,
		Center,
		End
	};
	
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
	};
	
private:
	struct Glyph
	{
		uint16_t bezierAtlasPos[2]; // XY pixel coordinates
		int16_t atlasIndex;
		int16_t offset[2]; // Amount to shift after character, [-1,1] range
		int16_t kern;
	};
	
	struct GlyphVertex
	{
		// XY coords of the vertex in range [-1,1]
		int16_t pos[2];
		
		// The UV coords of the data for this glyph in the bezier atlas
		// Also contains a vertex-dependent norm coordiate by encoding:
		// encode: data[n] = coord[n] * 2 + norm[n]
		// decode: norm[n] = data[n] % 2,  coord[n] = int(data[n] / 2)
		uint16_t data[2];
		
		// RGBA color [0,255]
		uint8_t color[4];
	};
	
	std::vector<AtlasGroup> atlases;
	std::map<uint32_t, Glyph> glyphs;
	std::vector<GlyphVertex> verts;
	
	std::string text;
	double x, y;
	bool showingCursor;
	Align horzAlign;
	Align vertAlign;
	
	AtlasGroup * GetOpenAtlasGroup();
	uint32_t LoadCodepointRange(FT_Face face, uint32_t start, uint32_t length);
	size_t GetNumLines();
	
public:
	GLLabel();
	GLLabel(std::string text);	
	~GLLabel();
	
	void SetText(std::string text);
	void AppendText(std::string text);
	
	void SetPosition(double x, double y);
	void SetHorzAlignment(Align horzAlign);
	void SetVertAlignment(Align vertAlign);
	void ShowCursor(bool show);
	
	void Render(float time);
};