#pragma once
#include <vector>
#include "types.hpp"

struct GridGlyph {
	// For each cell, a list of bezier curves (indices referring to input
	// bezier array) that pass through that cell.
	std::vector<std::vector<size_t>> cellBeziers;

	// For each cell, a boolean indicating whether the cell's midpoint is
	// inside the glpyh (true) or outside (false).
	std::vector<char> cellMids;

	// Size of the grid. Both arrays above are size width*height;
	int width;
	int height;

	GridGlyph(
		std::vector<Bezier2> &beziers,
		Vec2 glyphSize,
		int gridWidth,
		int gridHeight);
};
