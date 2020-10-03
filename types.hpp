struct Vec2 {
	union {
		float x;
		float w;
	};
	union {
		float y;
		float h;
	};

	Vec2(float x, float y): x(x), y(y) { }
};

// Second-order (aka quadratic or conic or single-control-point) bezier
struct Bezier2 {
	Vec2 e0;
	Vec2 e1;
	Vec2 c; // control point

	int intersectHorz(float y, float outX[2]);
	int intersectVert(float x, float outY[2]);
};

