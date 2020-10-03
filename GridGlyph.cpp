#include "GridGlyph.hpp"
#include <set>
#include <cmath>

template<class T>
constexpr const T& clamp(const T &v, const T &min, const T &max) {
	return std::max(std::min(v, max), min);
}

GridGlyph::GridGlyph(
	std::vector<Bezier2> &beziers,
	Vec2 glyphSize,
	int gridWidth,
	int gridHeight)
: width(gridWidth), height(gridHeight)
{
	this->cellBeziers.resize(this->width * this->height);
	this->cellMids.resize(this->width * this->height);

	auto setgrid = [this](int x, int y, size_t bezierIndex) {
		x = clamp(x, 0, this->width);
		y = clamp(y, 0, this->height);
		this->cellBeziers[(y * this->width) + x].push_back(bezierIndex);
	};

	// 1. Find which beziers intersect which grid cells
	for (size_t i = 0; i < beziers.size(); i++) {

		// Every vertical grid line including edges
		for (int x = 0; x <= this->width; x++) {
			float intY[2];
			int numInt = beziers[i].intersectVert(
				x * glyphSize.w / this->width,
				intY);
			for (int j = 0; j < numInt; j++) {
				int y = intY[j] * this->height / glyphSize.h;
				setgrid(x,     y, i); // left
				setgrid(x - 1, y, i); // right
			}
		}

		// Every horizontal grid line including edges
		for (int y = 0; y <= this->height; y++) {
			float intX[2];
			int numInt = beziers[i].intersectHorz(
				y * glyphSize.h / this->height,
				intX);
			for (int j = 0; j < numInt; j++) {
				int x = intX[j] * this->width / glyphSize.w;
				setgrid(x, y,      i); // up
				setgrid(x, y - 1 , i); // down
			}
		}

	}

	// 2. Find whether the center of each cell is inside the glyph
	for (size_t y = 0; y < gridHeight; y++) {

		// Find all intersections with cells horizontal midpoint line
		// and store them sorted from left to right
		std::set<float> intersections;
		float yMid = y + 0.5;
		for (size_t i = 0; i < beziers.size(); i++) {
			float intX[2];
			int numInt = beziers[i].intersectHorz(
				yMid * glyphSize.h / this->height,
				intX);
			for (int j = 0; j < numInt; j++) {
				float x = intX[j] * this->width / glyphSize.w;
				intersections.insert(x);
			}
		}

		// Traverse intersections (whole grid row, left to right).
		// Every 2nd crossing represents exiting an 'inside' region.
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
				int startCell = clamp((int)std::round(start), 0, this->width);
				int endCell = clamp((int)std::round(end), 0, this->width);
				for (int x = startCell; x < endCell; x++) {
					this->cellMids[(y * this->width) + x] = true;
				}
			}

			outside = !outside;
			start = end;
		}
	}
}
