#include <cmath>
#include "types.hpp"

inline bool almostEqual(float a, float b)
{
	return std::fabs(a-b) < 1e-5;
}

/*
 * Taking a quadratic bezier curve and a horizontal line y=Y, finds the x
 * values of intersection of the line and the curve. Returns 0, 1, or 2,
 * depending on how many intersections were found, and outX is filled with
 * that many x values of intersection.
 *
 * Quadratic bezier curves are represented by the function
 * F(t) = (1-t)^2*A + 2*t*(1-t)*B + t^2*C
 * where F is a vector function, A and C are the endpoint vectors, C is
 * the control point vector, and 0 <= t <= 1.
 * Solving the bezier function for t gives:
 * t = (A - B [+-] sqrt(y*a + B^2 - A*C))/a , where  a = A - 2B + C.
 * http://www.wolframalpha.com/input/?i=y+%3D+(1-t)%5E2a+%2B+2t(1-t)*b+%2B+t%5E2*c+solve+for+t
 */
int Bezier2::IntersectHorz(float Y, float outX[2])
{
	Vec2 A = this->e0;
	Vec2 B = this->c;
	Vec2 C = this->e1;
	int i = 0;

#define T_VALID(t) ((t) <= 1 && (t) >= 0)
#define X_FROM_T(t) ((1-(t))*(1-(t))*A.x + 2*(t)*(1-(t))*B.x + (t)*(t)*C.x)

	// Parts of the bezier function solved for t
	float a = A.y - 2*B.y + C.y;

	// In the condition that a=0, the standard formulas won't work
	if (almostEqual(a, 0)) {
		float t = (2*B.y - C.y - Y) / (2*(B.y-C.y));
		if (T_VALID(t)) {
			outX[i++] = X_FROM_T(t);
		}
		return i;
	}

	float sqrtTerm = std::sqrt(Y*a + B.y*B.y - A.y*C.y);

	float t = (A.y - B.y + sqrtTerm) / a;
	if (T_VALID(t)) {
		outX[i++] = X_FROM_T(t);
	}

	t = (A.y - B.y - sqrtTerm) / a;
	if (T_VALID(t)) {
		outX[i++] = X_FROM_T(t);
	}

	return i;

#undef X_FROM_T
#undef T_VALID
}

/*
 * Same as IntersectHorz, except finds the y values of an intersection
 * with the vertical line x=X.
 */
int Bezier2::IntersectVert(float X, float outY[2])
{
	Bezier2 inverse = {
		{ this->e0.y, this->e0.x },
		{ this->e1.y, this->e1.x },
		{ this->c.y, this->c.x }
	};
	return inverse.IntersectHorz(X, outY);
}

