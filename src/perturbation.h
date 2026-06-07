#ifndef __PERTURBATION_H__
#define __PERTURBATION_H__

// Perturbation-based Mandelbrot renderer.
//
// Standard double precision runs out around a window width of ~1e-13 (the
// pixel coordinates can no longer be told apart). Perturbation theory lifts
// that limit: it computes ONE high-precision "reference" orbit Z_n for the
// frame center c0, then every pixel c = c0 + dc is iterated as a small delta
//
//     d_{n+1} = 2*Z_n*d_n + d_n^2 + dc
//
// using only ordinary doubles (dc and d_n are tiny). |z_n| = |Z_n + d_n| is
// used for the escape test. Zhuoran's rebasing keeps the delta well-scaled
// and avoids the classic perturbation "glitches".
//
// The reference orbit itself is computed in double-double (~31 digits, see
// dd.h), so the achievable depth is bounded by that, roughly width ~1e-26.

// Render one frame into 'buf' (W*H bytes, one iteration-count byte per pixel,
// 0 = inside the set / black), centered at (cx,cy) with the given complex-plane
// width/height. maxiter caps the iteration count. Thread-safe; parallelised
// internally with OpenMP when available.
void mandelPerturbation(
    double cx, double cy, double width, double height,
    unsigned char *buf, int W, int H, int maxiter);

// Deep-zoom autopilot: zoom far past the double-precision limit using the
// perturbation renderer, presenting each frame. 'percent' is the fraction of
// pixels recomputed per frame (higher = crisper contours, slower). Returns
// average fps.
double deepAutopilot(double percent, bool benchmark);

// Interactive deep zoom (perturbation). Holds a full-quality frame when idle,
// so a deep location can be screenshotted. Returns average fps.
double deepMousedriven(double percent);

// Lower-level helpers, exposed for the temporal-reuse deep renderer.
//
// Compute the reference orbit for center (cx,cy) into refx/refy (each at least
// maxiter+1 doubles). Returns the stored length (Z_0 = (0,0) always).
int perturbComputeReference(
    double cx, double cy, double *refx, double *refy, int maxiter);

// Iterate one pixel given its delta-c (dcx,dcy) from the reference center,
// using a previously computed reference orbit. Returns the escape iteration
// (0 = inside the set).
int perturbPixelDelta(
    const double *refx, const double *refy, int reflen,
    double dcx, double dcy, int maxiter);

#endif
