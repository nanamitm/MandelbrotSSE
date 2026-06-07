#include <stdlib.h>

#include "dd.h"
#include "perturbation.h"

#ifdef __x86_64__
#include <immintrin.h>
#endif

// Compute the reference orbit Z_n for center (cx,cy) in double-double
// precision, storing the (double-rounded) values in refx/refy. Returns the
// number of stored iterations: maxiter, or fewer if the reference escaped.
// Note Z_0 = (0,0) always, which the renderers rely on when rebasing.
static int computeReferenceOrbit(
    double cx, double cy, double *refx, double *refy, int maxiter)
{
    dd Zx = dd_from(0.0), Zy = dd_from(0.0);
    dd Cx = dd_from(cx),  Cy = dd_from(cy);

    int n = 0;
    for (; n < maxiter; n++) {
        refx[n] = dd_to_double(Zx);
        refy[n] = dd_to_double(Zy);

        // Escape? (the reference is usually near the set, so this is rare)
        double zx = refx[n], zy = refy[n];
        if (zx*zx + zy*zy > 4.0) { n++; break; }

        // Z = Z^2 + C  (complex, in double-double)
        dd zx2 = dd_mul(Zx, Zx);
        dd zy2 = dd_mul(Zy, Zy);
        dd two_xy = dd_mul(dd_mul(Zx, Zy), dd_from(2.0));
        dd nZx = dd_add(dd_sub(zx2, zy2), Cx);
        dd nZy = dd_add(two_xy, Cy);
        Zx = nZx; Zy = nZy;
    }
    return n;
}

// Scalar perturbation for a single pixel. dc is the pixel offset from the
// reference (frame center). Returns the escape iteration (0 = inside set).
static inline int perturbPixel(
    const double *refx, const double *refy, int reflen,
    double dcx, double dcy, int maxiter)
{
    double dzx = 0.0, dzy = 0.0;
    int m = 0;
    for (int k = 0; k < maxiter; k++) {
        double zx = refx[m] + dzx;
        double zy = refy[m] + dzy;
        double z2 = zx*zx + zy*zy;
        if (z2 > 4.0) return k;                 // escaped at iteration k

        double dz2 = dzx*dzx + dzy*dzy;
        if (z2 < dz2 || m == reflen - 1) {      // Zhuoran rebasing
            dzx = zx; dzy = zy;
            m = 0;
        }
        double zmx = refx[m], zmy = refy[m];
        double ndzx = 2.0*(zmx*dzx - zmy*dzy) + (dzx*dzx - dzy*dzy) + dcx;
        double ndzy = 2.0*(zmx*dzy + zmy*dzx) + (2.0*dzx*dzy)       + dcy;
        dzx = ndzx; dzy = ndzy;
        m++;
    }
    return 0;                                   // inside the set
}

static void perturbRowsScalar(
    const double *refx, const double *refy, int reflen,
    unsigned char *buf, int W, int H, int maxiter,
    double stepx, double stepy, double cx0, double cy0)
{
#ifdef USE_OPENMP
#pragma omp parallel for schedule(dynamic,1)
#endif
    for (int py = 0; py < H; py++) {
        double dcy = cy0 - py * stepy;
        unsigned char *row = buf + (size_t)py * W;
        for (int px = 0; px < W; px++)
            row[px] = (unsigned char)perturbPixel(
                refx, refy, reflen, cx0 + px*stepx, dcy, maxiter);
    }
}

#ifdef __x86_64__
// AVX2+FMA perturbation: 4 pixels (one row chunk) at a time. The reference
// index m is shared across the 4 lanes (they normally advance together), so
// Z_m is a cheap broadcast load instead of a slow per-lane gather. Rebasing
// is done for the whole vector whenever ANY lane needs it (z2 < dz2) or the
// reference is exhausted (m == reflen-1); over-rebasing a lane is always
// mathematically valid (it just re-expresses z against Z_0 = 0), so this stays
// correct while keeping m scalar.
__attribute__((target("avx2,fma")))
static void perturbRowsAVX(
    const double *refx, const double *refy, int reflen,
    unsigned char *buf, int W, int H, int maxiter,
    double stepx, double stepy, double cx0, double cy0)
{
    const __m256d four = _mm256_set1_pd(4.0);
    const __m256d two  = _mm256_set1_pd(2.0);
    const __m256d zero = _mm256_setzero_pd();
    const int Wv = W & ~3;   // largest multiple of 4

#ifdef USE_OPENMP
#pragma omp parallel for schedule(dynamic,1)
#endif
    for (int py = 0; py < H; py++) {
        double dcyS = cy0 - py * stepy;
        __m256d dcy = _mm256_set1_pd(dcyS);
        unsigned char *row = buf + (size_t)py * W;

        for (int px = 0; px < Wv; px += 4) {
            double base = cx0 + px * stepx;
            __m256d dcx = _mm256_set_pd(base + 3*stepx, base + 2*stepx,
                                        base + 1*stepx, base);
            __m256d dzx = zero, dzy = zero;
            __m256i k1 = _mm256_setzero_si256();
            __m256d active = _mm256_castsi256_pd(_mm256_set1_epi64x(-1));
            int m = 0;

            for (int k = 0; k < maxiter; k++) {
                __m256d Zmx = _mm256_set1_pd(refx[m]);   // broadcast (no gather)
                __m256d Zmy = _mm256_set1_pd(refy[m]);
                __m256d zx = _mm256_add_pd(Zmx, dzx);
                __m256d zy = _mm256_add_pd(Zmy, dzy);
                __m256d z2 = _mm256_fmadd_pd(zx, zx, _mm256_mul_pd(zy, zy));

                // escape: z2 > 4 (only newly-escaped, still-active lanes record k)
                __m256d esc = _mm256_cmp_pd(z2, four, _CMP_GT_OQ);
                __m256d newesc = _mm256_and_pd(active, esc);
                __m256i kvec = _mm256_set1_epi64x(k);
                k1 = _mm256_castpd_si256(_mm256_blendv_pd(
                        _mm256_castsi256_pd(k1), _mm256_castsi256_pd(kvec), newesc));
                active = _mm256_andnot_pd(esc, active);
                if (_mm256_movemask_pd(active) == 0) break;

                // rebase whole vector if any lane needs it or reference ran out
                __m256d dz2 = _mm256_fmadd_pd(dzx, dzx, _mm256_mul_pd(dzy, dzy));
                __m256d glitch = _mm256_cmp_pd(z2, dz2, _CMP_LT_OQ);
                if (_mm256_movemask_pd(glitch) != 0 || m == reflen - 1) {
                    dzx = zx; dzy = zy;              // d <- z (true value)
                    Zmx = zero; Zmy = zero;          // Z_0 = 0
                    m = 0;
                }

                // d' = 2*Z_m*d + d^2 + dc   (complex)
                __m256d a = _mm256_fmsub_pd(Zmx, dzx, _mm256_mul_pd(Zmy, dzy));
                __m256d b = _mm256_fmsub_pd(dzx, dzx, _mm256_mul_pd(dzy, dzy));
                __m256d ndzx = _mm256_fmadd_pd(two, a, _mm256_add_pd(b, dcx));
                __m256d c = _mm256_fmadd_pd(Zmx, dzy, _mm256_mul_pd(Zmy, dzx));
                __m256d e = _mm256_fmadd_pd(dzx, dzy, c);
                __m256d ndzy = _mm256_fmadd_pd(two, e, dcy);
                dzx = ndzx; dzy = ndzy;
                m++;
            }

            long long kk[4];
            _mm256_storeu_si256((__m256i*)kk, k1);
            row[px+0] = (unsigned char)kk[0];
            row[px+1] = (unsigned char)kk[1];
            row[px+2] = (unsigned char)kk[2];
            row[px+3] = (unsigned char)kk[3];
        }
        // remainder columns (W not a multiple of 4)
        for (int px = Wv; px < W; px++)
            row[px] = (unsigned char)perturbPixel(
                refx, refy, reflen, cx0 + px*stepx, dcyS, maxiter);
    }
}
#endif

// Public wrappers for the temporal-reuse deep renderer (see xaos.cc).
int perturbComputeReference(
    double cx, double cy, double *refx, double *refy, int maxiter)
{
    return computeReferenceOrbit(cx, cy, refx, refy, maxiter);
}

int perturbPixelDelta(
    const double *refx, const double *refy, int reflen,
    double dcx, double dcy, int maxiter)
{
    return perturbPixel(refx, refy, reflen, dcx, dcy, maxiter);
}

void mandelPerturbation(
    double cx, double cy, double width, double height,
    unsigned char *buf, int W, int H, int maxiter)
{
    // The reference orbit is shared by every pixel: compute it once.
    double *refx = (double*)malloc(sizeof(double) * (maxiter + 1));
    double *refy = (double*)malloc(sizeof(double) * (maxiter + 1));
    int reflen = computeReferenceOrbit(cx, cy, refx, refy, maxiter);

    const double stepx = width / W;
    const double stepy = height / H;
    // Per-pixel delta is measured from the frame center (the reference).
    const double cx0 = -0.5 * (W - 1) * stepx;
    const double cy0 =  0.5 * (H - 1) * stepy;

#ifdef __x86_64__
    if (reflen >= 1 && __builtin_cpu_supports("avx2") && __builtin_cpu_supports("fma"))
        perturbRowsAVX(refx, refy, reflen, buf, W, H, maxiter, stepx, stepy, cx0, cy0);
    else
#endif
        perturbRowsScalar(refx, refy, reflen, buf, W, H, maxiter, stepx, stepy, cx0, cy0);

    free(refx);
    free(refy);
}
