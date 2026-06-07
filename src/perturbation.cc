#include <stdlib.h>

#include "dd.h"
#include "perturbation.h"

// Compute the reference orbit Z_n for center (cx,cy) in double-double
// precision, storing the (double-rounded) values in refx/refy. Returns the
// number of stored iterations: maxiter, or fewer if the reference escaped.
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

#ifdef USE_OPENMP
#pragma omp parallel for schedule(dynamic,1)
#endif
    for (int py = 0; py < H; py++) {
        double dcy = cy0 - py * stepy;             // delta-c imaginary part
        unsigned char *row = buf + (size_t)py * W;
        for (int px = 0; px < W; px++) {
            double dcx = cx0 + px * stepx;          // delta-c real part

            double dzx = 0.0, dzy = 0.0;            // delta orbit
            int m = 0;                              // index into reference
            int k1 = 0;                             // escape iteration (0=inside)

            for (int k = 0; k < maxiter; k++) {
                // Full value z_k = Z_m + dz
                double zx = refx[m] + dzx;
                double zy = refy[m] + dzy;
                double z2 = zx*zx + zy*zy;
                if (z2 > 4.0) { k1 = k; break; }    // escaped at iteration k

                // Rebase (Zhuoran): if the delta has grown to the size of the
                // full value, or we hit the end of the stored reference, treat
                // z_k itself as the new delta against Z_0 = 0.
                double dz2 = dzx*dzx + dzy*dzy;
                if (z2 < dz2 || m == reflen - 1) {
                    dzx = zx; dzy = zy;
                    m = 0;
                }

                // d_{k+1} = 2*Z_m*d + d^2 + dc   (complex)
                double zmx = refx[m], zmy = refy[m];
                double ndzx = 2.0*(zmx*dzx - zmy*dzy) + (dzx*dzx - dzy*dzy) + dcx;
                double ndzy = 2.0*(zmx*dzy + zmy*dzx) + (2.0*dzx*dzy)       + dcy;
                dzx = ndzx; dzy = ndzy;
                m++;
            }
            row[px] = (unsigned char)k1;
        }
    }

    free(refx);
    free(refy);
}
