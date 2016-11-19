#include <memory>
#include <map>

struct GLGlyph
{
	double width, height;
	double xOffset, yOffset;
	double kernX;
	
	size_t texIndex;
	double texU, texV, texWidth, texHeight;
};

class GLFontCache
{
	static std::shared_ptr<GLFontCache> singleton;
	std::map<uint32_t, GLGlyph *> glyphs;
	
	GLFontCache();
	
public:
	GLFontCache(const GLFontCache&) = delete;
	GLFontCache& operator=(const GLFontCache&) = delete;
	
	static std::shared_ptr<GLFontCache> GetFontCache();
	
	GLGlyph * GetGlpyhForCodePoint(uint32_t point, double pt);
	void BindTexture(size_t texIndex);
};