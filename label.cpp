#include "label.hpp"

static char32_t readNextChar(const char **p, size_t *datalen);

GLLabel::GLLabel()
: GLLabel("")
{
}

GLLabel::GLLabel(std::string text)
: x(0), y(0), horzAlign(GLFontAlign::Start), vertAlign(GLFontAlign::Start), showingCursor(false)
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
		
		switch(c)
		{
		case '\n':
			this->glyphs.push_back(nullptr);
			break;
		case '\r': break; // Ignore \r
		default:
			GLGlyph *glyph = this->cache->GetGlpyhForCodePoint(c, 12);
			if(glyph)
				this->glyphs.push_back(glyph);
			break;
		}
	}
}

void GLLabel::Render(float time)
{
	
}

uint32_t GLLabel::LoadCodepointRange(FT_Face face, uint32_t start, uint32_t length)
{
	
}

size_t GLLabel::GetNumLines()
{
	size_t lines = 0;
	for(size_t i=0; i<this->glyphs.size(); ++i)
		if(this->glyphs[i] == NULL)
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