#include <string>
#include <vector>
#include "fontCache.hpp"

enum class GLFontAlign
{
	Start,
	Center,
	End
};

class GLLabel
{
	std::shared_ptr<GLFontCache> cache;
	
	double x, y;
	GLFontAlign horzAlign;
	GLFontAlign vertAlign;
	bool showingCursor;
	
	// An array of glyphs in this label. The pointers are owned by
	// the GLFontCache singleton, and should not be freed.
	// A NULL item in the list represents a line break.
	std::vector<GLGlyph *> glyphs;
	
	size_t GetNumLines();
	
public:
	GLLabel();
	GLLabel(std::string text);	
	~GLLabel();
	
	void SetText(std::string text);
	void AppendText(std::string text);
	
	void SetPosition(double x, double y);
	void SetHorzAlignment(GLFontAlign horzAlign);
	void SetVertAlignment(GLFontAlign vertAlign);
	void ShowCursor(bool show);
	
	void Render(float time);
};