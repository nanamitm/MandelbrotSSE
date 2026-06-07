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
// perturbation renderer, presenting each frame. Returns average fps.
double deepAutopilot(bool benchmark);

#endif
