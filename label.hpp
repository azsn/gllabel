#include <string>
#include <vector>

class GLLabel
{
public:
	enum class Align
	{
		Start,
		Center,
		End
	};
	
private:
	
	struct AtlasGroup
	{
		GLuint bezierAtlasId, gridAtlasId;
		uint16_t nextBezierPos[2]; // UV
		uint16_t nextGridPos[2]; // UV
		bool full; // Just to make checking faster
	};
	
	struct Glyph
	{
		uint16_t atlasPos[2]; // UV
		int16_t offset[2]; // Amount to shift after character, [-1,1] range
		int16_t kern;
		int16_t atlasIndex;
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