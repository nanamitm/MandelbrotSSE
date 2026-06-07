#ifndef __DD_H__
#define __DD_H__

// Minimal double-double arithmetic (~31 significant digits). Just enough to
// compute an accurate Mandelbrot reference orbit for the perturbation-based
// deep zoom (see perturbation.cc). Each value is an unevaluated sum hi+lo
// with |lo| <= 0.5 ulp(hi).

struct dd { double hi, lo; };

static inline dd dd_from(double a) { dd r; r.hi = a; r.lo = 0.0; return r; }
static inline double dd_to_double(dd a) { return a.hi + a.lo; }

// Knuth's exact two-sum: hi+lo == a+b.
static inline dd two_sum(double a, double b) {
    double s  = a + b;
    double bb = s - a;
    double err = (a - (s - bb)) + (b - bb);
    dd r; r.hi = s; r.lo = err; return r;
}

// Exact product using FMA: hi+lo == a*b.
static inline dd two_prod(double a, double b) {
    double p = a * b;
    double e = __builtin_fma(a, b, -p);
    dd r; r.hi = p; r.lo = e; return r;
}

static inline dd dd_add(dd a, dd b) {
    dd s = two_sum(a.hi, b.hi);
    s.lo += a.lo + b.lo;
    return two_sum(s.hi, s.lo);      // renormalize
}

static inline dd dd_add_d(dd a, double b) {
    dd s = two_sum(a.hi, b);
    s.lo += a.lo;
    return two_sum(s.hi, s.lo);
}

static inline dd dd_sub(dd a, dd b) {
    dd nb; nb.hi = -b.hi; nb.lo = -b.lo;
    return dd_add(a, nb);
}

static inline dd dd_mul(dd a, dd b) {
    dd p = two_prod(a.hi, b.hi);
    p.lo += a.hi * b.lo + a.lo * b.hi;
    return two_sum(p.hi, p.lo);
}

#endif
