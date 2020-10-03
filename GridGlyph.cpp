#include "GridGlyph.hpp"
#include <cmath>

template<class T>
constexpr const T& clamp(const T &v, const T &min, const T &max) {
	return std::max(std::min(v, max), min);
}

// Returns a list of the beziers that intersect each grid cell.
// The returned outer vector is always size gridWidth*gridHeight.
static std::vector<std::set<size_t>> find_cells_intersections(
	std::vector<Bezier2> &beziers,
	Vec2 glyphSize,
	int gridWidth,
	int gridHeight)
{
	std::vector<std::set<size_t>> cellBeziers;
	cellBeziers.resize(gridWidth * gridHeight);

	auto setgrid = [&](int x, int y, size_t bezierIndex) {
		x = clamp(x, 0, gridWidth - 1);
		y = clamp(y, 0, gridHeight - 1);
		cellBeziers[(y * gridWidth) + x].insert(bezierIndex);
	};

	for (size_t i = 0; i < beziers.size(); i++) {
		bool anyIntersections = false;

		// Every vertical grid line including edges
		for (int x = 0; x <= gridWidth; x++) {
			float intY[2];
			int numInt = beziers[i].IntersectVert(
				x * glyphSize.w / gridWidth,
				intY);
			for (int j = 0; j < numInt; j++) {
				int y = intY[j] * gridHeight / glyphSize.h;
				setgrid(x,     y, i); // right
				setgrid(x - 1, y, i); // left
				anyIntersections = true;
			}
		}

		// Every horizontal grid line including edges
		for (int y = 0; y <= gridHeight; y++) {
			float intX[2];
			int numInt = beziers[i].IntersectHorz(
				y * glyphSize.h / gridHeight,
				intX);
			for (int j = 0; j < numInt; j++) {
				int x = intX[j] * gridWidth / glyphSize.w;
				setgrid(x, y,      i); // up
				setgrid(x, y - 1 , i); // down
				anyIntersections = true;
			}
		}

		// If no grid line intersections, bezier is fully contained in
		// one cell. Mark this bezier as intersecting that cell.
		if (!anyIntersections) {
			int x = beziers[i].e0.x * gridWidth / glyphSize.w;
			int y = beziers[i].e0.y * gridHeight / glyphSize.h;
			setgrid(x, y, i);
		}
	}

	return cellBeziers;
}

// Returns whether the midpoint of the cell is inside the glyph for each cell.
// The returned vector is always size gridWidth*gridHeight.
static std::vector<char> find_cells_mids_inside(
	std::vector<Bezier2> &beziers,
	Vec2 glyphSize,
	int gridWidth,
	int gridHeight)
{
	std::vector<char> cellMids;
	cellMids.resize(gridWidth * gridHeight);

	// Find whether the center of each cell is inside the glyph
	for (size_t y = 0; y < gridHeight; y++) {
		// Find all intersections with cells horizontal midpoint line
		// and store them sorted from left to right
		std::set<float> intersections;
		float yMid = y + 0.5;
		for (size_t i = 0; i < beziers.size(); i++) {
			float intX[2];
			int numInt = beziers[i].IntersectHorz(
				yMid * glyphSize.h / gridHeight,
				intX);
			for (int j = 0; j < numInt; j++) {
				float x = intX[j] * gridWidth / glyphSize.w;
				intersections.insert(x);
			}
		}

		// Traverse intersections (whole grid row, left to right).
		// Every 2nd crossing represents exiting an "inside" region.
		// All properly formed glyphs should have an even number of
		// crossings.
		bool outside = false;
		float start = 0;
		for (auto it = intersections.begin(); it != intersections.end(); it++) {
			float end = *it;

			// Upon exiting, the midpoint of every cell between
			// start and end, rounded to the nearest int, is
			// inside the glyph.
			if (outside) {
				int startCell = clamp((int)std::round(start), 0, gridWidth);
				int endCell = clamp((int)std::round(end), 0, gridWidth);
				for (int x = startCell; x < endCell; x++) {
					cellMids[(y * gridWidth) + x] = true;
				}
			}

			outside = !outside;
			start = end;
		}
	}

	return cellMids;
}

GridGlyph::GridGlyph(
	std::vector<Bezier2> &beziers,
	Vec2 glyphSize,
	int gridWidth,
	int gridHeight)
: width(gridWidth), height(gridHeight)
{
	this->cellBeziers = find_cells_intersections(
		beziers, glyphSize, gridWidth, gridHeight);
	this->cellMids = find_cells_mids_inside(
		beziers, glyphSize, gridWidth, gridHeight);
}
