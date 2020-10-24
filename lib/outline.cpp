#include "outline.hpp"
#include "cubic2quad.hpp"
#include <algorithm>

struct DecomposeState
{
	std::vector<Bezier2> *curves;
	FT_Vector prev;
	int c2qResolution;
	double *c2qOut;
};

static Bezier2 vec2bezier(const FT_Vector *e0, const FT_Vector *c, const FT_Vector *e1)
{
	Bezier2 b;
	b.e0 = Vec2(e0->x, e0->y);
	b.e1 = Vec2(e1->x, e1->y);
	b.c = Vec2(c->x, c->y);
	return b;
}

static int decompose_move_to(const FT_Vector *to, void *user)
{
	auto state = static_cast<DecomposeState *>(user);
	state->prev = *to;
	return 0;
}

static int decompose_line_to(const FT_Vector *to, void *user)
{
	auto state = static_cast<DecomposeState *>(user);
	state->curves->push_back(vec2bezier(&state->prev, &state->prev, to));
	state->prev = *to;
	return 0;
}

static int decompose_conic_to(
	const FT_Vector *c1, const FT_Vector *to, void *user)
{
	auto state = static_cast<DecomposeState *>(user);
	state->curves->push_back(vec2bezier(&state->prev, c1, to));
	state->prev = *to;
	return 0;
}

static int decompose_cubic_to(
	const FT_Vector *c1, const FT_Vector *c2, const FT_Vector *to, void *user)
{
	auto state = static_cast<DecomposeState *>(user);

	double in[8] = {
		(double)state->prev.x, (double)state->prev.y,
		(double)c1->x, (double)c1->y,
		(double)c2->x, (double)c2->y,
		(double)to->x, (double)to->y,
	};
	double *out = state->c2qOut;

	int nvals = 6 * cubic2quad(in, state->c2qResolution, out);

	for (int i = 0; i < nvals; i += 6) {
		Bezier2 b;
		b.e0 = Vec2(out[i], out[i+1]);
		b.c = Vec2(out[i+2], out[i+3]);
		b.e1 = Vec2(out[i+4], out[i+5]);
		state->curves->push_back(b);
	}

	state->prev = *to;
	return 0;
}

// Decompose an outline into an array of quadratic bezier curves. Cubics in
// the outline are converted to quadratic at the given resolution.
static std::vector<Bezier2> decompose(FT_Outline *outline, int c2qResolution)
{
	double c2qOut[C2Q_OUT_LEN];

	std::vector<Bezier2> curves;
	curves.reserve(outline->n_contours);

	DecomposeState state{};
	state.curves = &curves;
	state.c2qOut = c2qOut;
	state.c2qResolution = c2qResolution;

	FT_Outline_Funcs funcs{};
	funcs.move_to = decompose_move_to;
	funcs.line_to = decompose_line_to;
	funcs.conic_to = decompose_conic_to;
	funcs.cubic_to = decompose_cubic_to;

	if (FT_Outline_Decompose(outline, &funcs, &state) != 0) {
		return std::vector<Bezier2>();
	}
	return curves;
}

// Shifts all bezier points so 'origin' becomes 0,0
static void translate_beziers(std::vector<Bezier2> &beziers, Vec2 origin)
{
	for (size_t i = 0; i < beziers.size(); i++) {
		beziers[i].e0.x -= origin.x;
		beziers[i].e0.y -= origin.y;
		beziers[i].e1.x -= origin.x;
		beziers[i].e1.y -= origin.y;
		beziers[i].c.x -= origin.x;
		beziers[i].c.y -= origin.y;
	}
}

// Converts a counterclockwise outline to clockwise one.
static void flip_beziers(std::vector<Bezier2> &beziers)
{
	for (size_t i = 0; i < beziers.size(); i++) {
		std::swap(beziers[i].e0, beziers[i].e1);
	}
}

// Convert a FreeType Outline into an array of quadratic beziers. For well-
// designed fonts, the beziers are always generated clockwise (fill right).
std::vector<Bezier2> GetBeziersForOutline(FT_Outline *outline)
{
	if (!outline || outline->n_points <= 0) {
		return std::vector<Bezier2>();
	}

	FT_BBox cbox;
	FT_Outline_Get_CBox(outline, &cbox);
	FT_Pos width = cbox.xMax - cbox.xMin;
	FT_Pos height = cbox.yMax - cbox.yMin;

	// Tolerance for error when approxmating cubic beziers with quadratics.
	// Too low and many quadratics are generated (slow), too high and not
	// enough are generated (looks bad). 5% works pretty well.
	int c2qResolution = std::max((int)(((width + height) / 2) * 0.05), 1);

	std::vector<Bezier2> beziers = decompose(outline, c2qResolution);

	if (cbox.xMin != 0 || cbox.yMin != 0) {
		translate_beziers(beziers, Vec2(cbox.xMin, cbox.yMin));
	}

	FT_Orientation orientation = FT_Outline_Get_Orientation(outline);
	bool counterclockwise = (orientation == FT_ORIENTATION_FILL_LEFT);
	if (counterclockwise) {
		flip_beziers(beziers);
	}

	return beziers;
}
