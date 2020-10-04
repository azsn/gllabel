#pragma once
#include "types.hpp"
#include <vector>
#include <set>

// Reprents a grid that is "overlayed" on top of a glyph, storing some
// properties about each grid cell. The grid's origin is bottom-left
// and is stored in row-major order.
struct VGrid {
	// For each cell, a set of bezier curves (indices referring to
	// input bezier array) that pass through that cell.
	std::vector<std::set<size_t>> cellBeziers;

	// For each cell, a boolean indicating whether the cell's midpoint is
	// inside the glpyh (true) or outside (false).
	std::vector<char> cellMids;

	// Size of the grid. Both arrays above are size width*height.
	int width;
	int height;

	VGrid(
		std::vector<Bezier2> &beziers,
		Vec2 glyphSize,
		int gridWidth,
		int gridHeight);
};

struct VGridAtlas {
	// 2D buffer, size is width*height, row-major, starts at bottom-left
	uint8_t *data;

	uint16_t width;
	uint16_t height;

	// Bytes per pixel, aka. how many bezier curves are allowed per grid
	// cell. This should probably always be 4, since that's the limit of
	// bytes per pixel that OpenGL supports (GL_RGBA8).
	uint8_t depth;

	void WriteVGridAt(VGrid &grid, uint16_t atX, uint16_t atY);
};
