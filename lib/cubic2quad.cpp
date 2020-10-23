// From github.com/zelbrium/cubic2quad

// Copyright (C) 2015 by Vitaly Puzrin   (original JavaScript version)
// Copyright (C) 2020 zelbrium           (this C version)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the “Software”), to
// deal in the Software without restriction, including without limitation the
// rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
// sell copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED “AS IS”, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
// IN THE SOFTWARE.

#include <math.h>
#include <stdbool.h>

#define UNUSED(x) (void)(x)
#define PRECISION 1e-8

typedef struct {
	double x;
	double y;
} Point;

typedef struct {
	Point p1;
	Point c1;
	Point p2;
} QBezier;

typedef struct {
	Point p1;
	Point c1;
	Point c2;
	Point p2;
} CBezier;

static Point p_new(const double x, const double y)
{
	Point p;
	p.x = x;
	p.y = y;
	return p;
}

static Point p_add(const Point a, const Point b)
{
	return p_new(a.x + b.x, a.y + b.y);
}

static Point p_sub(const Point a, const Point b)
{
	return p_new(a.x - b.x, a.y - b.y);
}

static Point p_mul(const Point a, const double value)
{
	return p_new(a.x * value, a.y * value);
}

static Point p_div(const Point a, const double value)
{
	return p_new(a.x / value, a.y / value);
}

static double p_dist(const Point a)
{
	return sqrt(a.x*a.x + a.y*a.y);
}

static double p_sqr(const Point a)
{
	return a.x*a.x + a.y*a.y;
}

static double p_dot(const Point a, const Point b)
{
	return a.x*b.x + a.y*b.y;
}

static void calc_power_coefficients(
	const Point p1, const Point c1, const Point c2, const Point p2,
	Point out[4])
{
	// point(t) = p1*(1-t)^3 + c1*t*(1-t)^2 + c2*t^2*(1-t) + p2*t^3 = a*t^3 + b*t^2 + c*t + d
	// for each t value, so
	// a = (p2 - p1) + 3 * (c1 - c2)
	// b = 3 * (p1 + c2) - 6 * c1
	// c = 3 * (c1 - p1)
	// d = p1
	const Point a = p_add(p_sub(p2, p1), p_mul(p_sub(c1, c2), 3));
	const Point b = p_sub(p_mul(p_add(p1, c2), 3), p_mul(c1, 6));
	const Point c = p_mul(p_sub(c1, p1), 3);
	const Point d = p1;
	out[0] = a;
	out[1] = b;
	out[2] = c;
	out[3] = d;
}

static Point calc_point(
	const Point a, const Point b, const Point c, const Point d, double t)
{
	// a*t^3 + b*t^2 + c*t + d = ((a*t + b)*t + c)*t + d
	return p_add(p_mul(p_add(p_mul(p_add(p_mul(a, t), b), t), c), t), d);
}

static Point calc_point_quad(
	const Point a, const Point b, const Point c, double t)
{
	// a*t^2 + b*t + c = (a*t + b)*t + c
	return p_add(p_mul(p_add(p_mul(a, t), b), t), c);
}

static Point calc_point_derivative(
	const Point a, const Point b, const Point c, const Point d, double t)
{
	UNUSED(d);
	// d/dt[a*t^3 + b*t^2 + c*t + d] = 3*a*t^2 + 2*b*t + c = (3*a*t + 2*b)*t + c
	return p_add(p_mul(p_add(p_mul(a, 3*t), p_mul(b, 2)), t), c);
}

static int quad_solve(
	const double a, const double b, const double c, double out[2])
{
	// a*x^2 + b*x + c = 0
	if (fabs(a) < PRECISION) {
		if (b == 0) {
			out[0] = 0;
			out[1] = 0;
			return 0;
		} else {
			out[0] = -c / b;
			out[1] = 0;
			return 1;
		}
	}
	const double D = b*b - 4*a*c;
	if (fabs(D) < PRECISION) {
		out[0] = -b/(2*a);
		out[1] = 0;
		return 1;
	} else if (D < 0) {
		out[0] = 0;
		out[1] = 0;
		return 0;
	}
	const double DSqrt = sqrt(D);
	out[0] = (-b - DSqrt) / (2*a);
	out[1] = (-b + DSqrt) / (2*a);
	return 2;
}

static double cubic_root(const double x)
{
	return (x < 0) ? -pow(-x, 1.0/3.0) : pow(x, 1.0/3.0);
}

static int cubic_solve(
	const double a, const double b, const double c, const double d,
	double out[3])
{
	// a*x^3 + b*x^2 + c*x + d = 0
	if (fabs(a) < PRECISION) {
		out[2] = 0;
		return quad_solve(b, c, d, out);
	}
	// solve using Cardan's method, which is described in paper of R.W.D. Nickals
	// http://www.nickalls.org/dick/papers/maths/cubic1993.pdf (doi:10.2307/3619777)
	const double xn = -b / (3*a); // point of symmetry x coordinate
	const double yn = ((a * xn + b) * xn + c) * xn + d; // point of symmetry y coordinate
	const double deltaSq = (b*b - 3*a*c) / (9*a*a); // delta^2
	const double hSq = 4*a*a * pow(deltaSq, 3);
	const double D3 = yn*yn - hSq;
	if (fabs(D3) < PRECISION) { // 2 real roots
		const double delta1 = cubic_root(yn/(2*a));
		out[0] = xn - 2 * delta1;
		out[1] = xn + delta1;
		out[2] = 0;
		return 2;
	} else if (D3 > 0) { // 1 real root
		const double D3Sqrt = sqrt(D3);
		out[0] = xn + cubic_root((-yn + D3Sqrt)/(2*a)) + cubic_root((-yn - D3Sqrt)/(2*a));
		out[1] = 0;
		out[2] = 0;
		return 1;
	}
	// 3 real roots
	const double theta = acos(-yn / sqrt(hSq)) / 3;
	const double delta = sqrt(deltaSq);
	out[0] = xn + 2 * delta * cos(theta);
	out[1] = xn + 2 * delta * cos(theta + M_PI * 2.0 / 3.0);
	out[2] = xn + 2 * delta * cos(theta + M_PI * 4.0 / 3.0);
	return 3;
}

static double min_distance_to_quad(
	const Point point, const Point p1, const Point c1, const Point p2)
{
	// f(t) = (1-t)^2 * p1 + 2*t*(1 - t) * c1 + t^2 * p2 = a*t^2 + b*t + c, t in [0, 1],
	// a = p1 + p2 - 2 * c1
	// b = 2 * (c1 - p1)
	// c = p1; a, b, c are vectors because p1, c1, p2 are vectors too
	// The distance between given point and quadratic curve is equal to
	// sqrt((f(t) - point)^2), so these expression has zero derivative by t at points where
	// (f'(t), (f(t) - point)) = 0.
	// Substituting quadratic curve as f(t) one could obtain a cubic equation
	// e3*t^3 + e2*t^2 + e1*t + e0 = 0 with following coefficients:
	// e3 = 2 * a^2
	// e2 = 3 * a*b
	// e1 = (b^2 + 2 * a*(c - point))
	// e0 = (c - point)*b
	// One of the roots of the equation from [0, 1], or t = 0 or t = 1 is a value of t
	// at which the distance between given point and quadratic Bezier curve has minimum.
	// So to find the minimal distance one have to just pick the minimum value of
	// the distance on set {t = 0 | t = 1 | t is root of the equation from [0, 1] }.

	const Point a = p_sub(p_add(p1, p2), p_mul(c1, 2));
	const Point b = p_mul(p_sub(c1, p1), 2);
	const Point c = p1;
	const double e3 = 2 * p_sqr(a);
	const double e2 = 3 * p_dot(a, b);
	const double e1 = (p_sqr(b) + 2 * p_dot(a, p_sub(c, point)));
	const double e0 = p_dot(p_sub(c, point), b);

	double roots[3];
	const int nroots = cubic_solve(e3, e2, e1, e0, roots);

	double candidates[5];
	int nc = 0;
	for (int i = 0; i < nroots; i++) {
		if (roots[i] > PRECISION && roots[i] < 1 - PRECISION) {
			candidates[nc++] = roots[i];
		}
	}
	candidates[nc++] = 0;
	candidates[nc++] = 1;

	double minDistance = INFINITY;
	for (int i = 0; i < nc; i++) {
		const double distance = p_dist(p_sub(calc_point_quad(a, b, c, candidates[i]), point));
		if (distance < minDistance) {
			minDistance = distance;
		}
	}
	return minDistance;
}

static void process_segment(
	const Point a, const Point b, const Point c, const Point d,
	const double t1, const double t2,
	QBezier *out)
{
	// Find a single control point for given segment of cubic Bezier curve
	// These control point is an interception of tangent lines to the boundary points
	// Let's denote that f(t) is a vector function of parameter t that defines the cubic Bezier curve,
	// f(t1) + f'(t1)*z1 is a parametric equation of tangent line to f(t1) with parameter z1
	// f(t2) + f'(t2)*z2 is the same for point f(t2) and the vector equation
	// f(t1) + f'(t1)*z1 = f(t2) + f'(t2)*z2 defines the values of parameters z1 and z2.
	// Defining fx(t) and fy(t) as the x and y components of vector function f(t) respectively
	// and solving the given system for z1 one could obtain that
	//
	//      -(fx(t2) - fx(t1))*fy'(t2) + (fy(t2) - fy(t1))*fx'(t2)
	// z1 = ------------------------------------------------------.
	//            -fx'(t1)*fy'(t2) + fx'(t2)*fy'(t1)
	//
	// Let's assign letter D to the denominator and note that if D = 0 it means that the curve actually
	// is a line. Substituting z1 to the equation of tangent line to the point f(t1), one could obtain that
	// cx = [fx'(t1)*(fy(t2)*fx'(t2) - fx(t2)*fy'(t2)) + fx'(t2)*(fx(t1)*fy'(t1) - fy(t1)*fx'(t1))]/D
	// cy = [fy'(t1)*(fy(t2)*fx'(t2) - fx(t2)*fy'(t2)) + fy'(t2)*(fx(t1)*fy'(t1) - fy(t1)*fx'(t1))]/D
	// where c = (cx, cy) is the control point of quadratic Bezier curve.

	const Point f1 = calc_point(a, b, c, d, t1);
	const Point f2 = calc_point(a, b, c, d, t2);
	const Point f1_ = calc_point_derivative(a, b, c, d, t1);
	const Point f2_ = calc_point_derivative(a, b, c, d, t2);

	out->p1 = f1;
	out->p2 = f2;

	const double D = -f1_.x * f2_.y + f2_.x * f1_.y;
	if (fabs(D) < PRECISION) {
		// straight line segment
		out->c1 = p_div(p_add(f1, f2), 2);
		return;
	}
	const double cx = (f1_.x*(f2.y*f2_.x - f2.x*f2_.y) + f2_.x*(f1.x*f1_.y - f1.y*f1_.x)) / D;
	const double cy = (f1_.y*(f2.y*f2_.x - f2.x*f2_.y) + f2_.y*(f1.x*f1_.y - f1.y*f1_.x)) / D;
	out->c1 = p_new(cx, cy);
}

static bool is_segment_approximation_close(
	const Point a, const Point b, const Point c, const Point d,
	double tmin, double tmax,
	const Point p1, const Point c1, const Point p2,
	double errorBound)
{
	// a,b,c,d define cubic curve
	// tmin, tmax are boundary points on cubic curve
	// p1, c1, p2 define quadratic curve
	// errorBound is maximum allowed distance
	// Try to find maximum distance between one of N points segment of given cubic
	// and corresponding quadratic curve that estimates the cubic one, assuming
	// that the boundary points of cubic and quadratic points are equal.
	//
	// The distance calculation method comes from Hausdorff distance defenition
	// (https://en.wikipedia.org/wiki/Hausdorff_distance), but with following simplifications
	// * it looks for maximum distance only for finite number of points of cubic curve
	// * it doesn't perform reverse check that means selecting set of fixed points on
	//   the quadratic curve and looking for the closest points on the cubic curve
	// But this method allows easy estimation of approximation error, so it is enough
	// for practical purposes.

	const int n = 10; // number of points + 1
	const double dt = (tmax - tmin) / n;
	for (double t = tmin + dt; t < tmax - dt; t += dt) { // don't check distance on boundary points
	                                                     // because they should be the same
		const Point point = calc_point(a, b, c, d, t);
		if (min_distance_to_quad(point, p1, c1, p2) > errorBound) {
			return false;
		}
	}
	return true;
}

static bool _is_approximation_close(
	const Point a, const Point b, const Point c, const Point d,
	const QBezier * const quadCurves, const int quadCurvesLen,
	const double errorBound)
{
	const double dt = 1.0 / quadCurvesLen;
	for (int i = 0; i < quadCurvesLen; i++) {
		const Point p1 = quadCurves[i].p1;
		const Point c1 = quadCurves[i].c1;
		const Point p2 = quadCurves[i].p2;
		if (!is_segment_approximation_close(a, b, c, d, i * dt, (i + 1) * dt, p1, c1, p2, errorBound)) {
			return false;
		}
	}
	return true;
}

/*
 * Split cubic bézier curve into two cubic curves, see details here:
 * https://math.stackexchange.com/questions/877725
 */
static void subdivide_cubic(const CBezier *b, const double t, CBezier out[2])
{
	const double u = 1-t, v = t;
	
	const double bx = b->p1.x*u + b->c1.x*v;
	const double sx = b->c1.x*u + b->c2.x*v;
	const double fx = b->c2.x*u + b->p2.x*v;
	const double cx = bx*u + sx*v;
	const double ex = sx*u + fx*v;
	const double dx = cx*u + ex*v;
	
	const double by = b->p1.y*u + b->c1.y*v;
	const double sy = b->c1.y*u + b->c2.y*v;
	const double fy = b->c2.y*u + b->p2.y*v;
	const double cy = by*u + sy*v;
	const double ey = sy*u + fy*v;
	const double dy = cy*u + ey*v;

	out[0].p1 = p_new(b->p1.x, b->p1.y);
	out[0].c1 = p_new(bx, by);
	out[0].c2 = p_new(cx, cy);
	out[0].p2 = p_new(dx, dy);
	out[1].p1 = p_new(dx, dy);
	out[1].c1 = p_new(ex, ey);
	out[1].c2 = p_new(fx, fy);
	out[1].p2 = p_new(b->p2.x, b->p2.y);
}

#define MAX_INFLECTIONS (2)

/*
 * Find inflection points on a cubic curve, algorithm is similar to this one:
 * http://www.caffeineowl.com/graphics/2d/vectorial/cubic-inflexion.html
 */
static int solve_inflections(const CBezier *b, double out[MAX_INFLECTIONS])
{
	const double
		x1 = b->p1.x, y1 = b->p1.y,
		x2 = b->c1.x, y2 = b->c1.y,
		x3 = b->c2.x, y3 = b->c2.y,
		x4 = b->p2.x, y4 = b->p2.y;

	const double p = -(x4 * (y1 - 2 * y2 + y3)) + x3 * (2 * y1 - 3 * y2 + y4)
	           + x1 * (y2 - 2 * y3 + y4) - x2 * (y1 - 3 * y3 + 2 * y4);
	const double q = x4 * (y1 - y2) + 3 * x3 * (-y1 + y2) + x2 * (2 * y1 - 3 * y3 + y4) - x1 * (2 * y2 - 3 * y3 + y4);
	const double r = x3 * (y1 - y2) + x1 * (y2 - y3) + x2 * (-y1 + y3);

	double roots[2];
	const int nroots = quad_solve(p, q, r, roots);

	out[0] = 0;
	out[1] = 0;

	int ni = 0;
	for (int i = 0; i < nroots; i++) {
		if (roots[i] > PRECISION && roots[i] < 1 - PRECISION) {
			out[ni++] = roots[i];
		}
	}

	if (ni == 2 && out[0] > out[1]) { // sort ascending
		double t = out[1];
		out[1] = out[0];
		out[0] = t;
	}

	return ni;
}

#define MAX_SEGMENTS (8)

/*
 * Approximate cubic Bezier curve defined with base points p1, p2 and control points c1, c2 with
 * with a few quadratic Bezier curves.
 * The function uses tangent method to find quadratic approximation of cubic curve segment and
 * simplified Hausdorff distance to determine number of segments that is enough to make error small.
 * In general the method is the same as described here: https://fontforge.github.io/bezier.html.
 */
static int _cubic_to_quad(const CBezier *cb, double errorBound, QBezier approximation[MAX_SEGMENTS])
{
	Point pc[4];
	calc_power_coefficients(cb->p1, cb->c1, cb->c2, cb->p2, pc);
	const Point a = pc[0], b = pc[1], c = pc[2], d = pc[3];

	int segmentsCount = 1;
	for (; segmentsCount <= MAX_SEGMENTS; segmentsCount++) {
		for (int i = 0; i < segmentsCount; i++) {
			double t = (double)i/(double)segmentsCount;
			process_segment(a, b, c, d, t, t + 1.0/(double)segmentsCount, &approximation[i]);
		}
		if (segmentsCount == 1 && (
			p_dot(p_sub(approximation[0].c1, cb->p1), p_sub(cb->c1, cb->p1)) < 0 ||
			p_dot(p_sub(approximation[0].c1, cb->p2), p_sub(cb->c2, cb->p2)) < 0)) {
			// approximation concave, while the curve is convex (or vice versa)
			continue;
		}
		if (_is_approximation_close(a, b, c, d, approximation, segmentsCount, errorBound)) {
			return segmentsCount;
		}
	}
	return MAX_SEGMENTS;
}

// A cubic bezier can have up to two inflection points
// (e.g: [0, 0, 10, 20, 0, 10, 20, 20] has 2)
// leading to 3 overall sections to convert. This algorithm limits to 8 output
// quads segments per section (depending on errorBound), for a maximum of 24
// quads per input cubic.
#define MAX_QUADS_OUT (MAX_SEGMENTS * (MAX_INFLECTIONS + 1)) // 24

static int cubic_to_quad(const CBezier *cb, double errorBound, QBezier result[MAX_QUADS_OUT])
{
	double inflections[MAX_INFLECTIONS];
	int numInflections = solve_inflections(cb, inflections);

	if (numInflections == 0) {
		return _cubic_to_quad(cb, errorBound, result);
	}

	int nq = 0;

	CBezier curve = *cb;
	double prevPoint = 0;

	CBezier split[2];
	for (int inflectionIdx = 0; inflectionIdx < numInflections; inflectionIdx++) {
		subdivide_cubic(&curve,
			// we make a new curve, so adjust inflection point accordingly
			1 - (1 - inflections[inflectionIdx]) / (1 - prevPoint),
			split);

		nq += _cubic_to_quad(&split[0], errorBound, &result[nq]);

		curve = split[1];
		prevPoint = inflections[inflectionIdx];
	}

	nq += _cubic_to_quad(&curve, errorBound, &result[nq]);
	return nq;
}

// 24 quads * 3 points per quad * 2 doubles per point
#define MAX_DOUBLES_OUT (MAX_QUADS_OUT * 3 * 2) // 144 (1152 bytes)

// Converts the input cubic
// (8 doubles in p1x, p1y, c1x, c1y, c2x, c2y, p2x, p2y form)
// into up to 24 quadratics. The output buffer must be at least 144 doubles
// long for the 24 quadratics (6 bytes each).
int cubic2quad(const double in[8], const double errorBound, double out[MAX_DOUBLES_OUT])
{
	return cubic_to_quad((const CBezier *)in, errorBound, (QBezier *)out);
}
