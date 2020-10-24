#ifndef OUTLINE_H
#define OUTLINE_H

#include "types.hpp"
#include <vector>
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

std::vector<Bezier2> GetBeziersForOutline(FT_Outline *outline);

#endif
