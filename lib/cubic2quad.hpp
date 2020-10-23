// From github.com/zelbrium/cubic2quad

#ifndef _H_CUBIC2QUAD
#define _H_CUBIC2QUAD

// Minimum size of the cubic2quad() output buffer, in number of doubles.
#define C2Q_OUT_LEN 144

// cubic2quad generates a spline of quadratic beziers to approximate a single
// cubic bezier.
//
// NOTE: The output buffer must be at least (C2Q_OUT_LEN*sizeof(double)) bytes
// long.
//
// Parameters:
// in: The input cubic bezier in the form
//     p1x, p1y, c1x, c1y, c2x, c2y, p2x, p2y
//
// precision: How close the output spline should be to the original cubic.
//     Smaller values for precision will result in a more accurate spline but
//     will require more quadratic beziers to form it.
//
// out: The output quadratic beziers, each a repetition of 6 doubles
//     p1x, p1y, cx, cy, p2x, p2y
//     Note that (p2x,p2y) of one quadratic will always equal the (p1x,p1y)
//     of the next quadratic because they are placed end-to-end.
//
// Return value: The number of output quadratics written to `out`. `out` is
//     filled with [return value]*6 doubles. The contents of the remainder of
//     the buffer (total of C2Q_OUT_LEN doubles long) is undefined.
int cubic2quad(const double in[8], const double precision, double out[C2Q_OUT_LEN]);

#endif // _H_CUBIC2QUAD
