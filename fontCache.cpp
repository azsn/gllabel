#include "fontCache.hpp"

std::shared_ptr<GLFontCache> GLFontCache::singleton;

GLFontCache::GLFontCache()
{
}

std::shared_ptr<GLFontCache> GLFontCache::GetFontCache()
{
	if(GLFontCache::singleton)
		return GLFontCache::singleton;
	
	// Cannot use std::make_shared (easily) since GLFontCache has a private constructor
	return std::shared_ptr<GLFontCache>(new GLFontCache());
}

GLGlyph * GLFontCache::GetGlpyhForCodePoint(uint32_t codePoint, double pt)
{
	return nullptr;
}

// void GLFontCache::GenerateMap()