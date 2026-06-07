// Differential / sanity test for the SIMD core loops.
// Compares AVX vs SSE vs scalar over a grid covering the whole set,
// and prints a checksum of the AVX output so the FMA rewrite can be
// compared against the pre-FMA build.
#include <cstdio>
#include <cstdlib>
#include <chrono>

int iterations = 2048;   // global read by the core loops

#include "sse.h"

int main()
{
    const double xmin = -2.2, xmax = 0.8;
    const double ymin = -1.2, ymax = 1.2;
    const int W = 400, H = 320;          // W divisible by 4
    const double xstep = (xmax - xmin) / W;

    unsigned long long avx_sum = 0;
    long total = 0, mm_avx_sse = 0, mm_avx_def = 0;
    int max_avx_sse = 0, max_avx_def = 0;

    for (int j = 0; j < H; j++) {
        double y = ymin + j * (ymax - ymin) / H;
        for (int i = 0; i < W; i += 4) {
            double x = xmin + i * xstep;
            unsigned char d[4], s[4], a[4];
            unsigned char *pd = d, *ps = s, *pa = a;
            CoreLoopDoubleDefault(x, y, xstep, &pd);
            CoreLoopDoubleSSE(x, y, xstep, &ps);
            CoreLoopDoubleAVX(x, y, xstep, &pa);
            for (int k = 0; k < 4; k++) {
                total++;
                avx_sum += a[k];
                int e1 = abs((int)a[k] - (int)s[k]);
                int e2 = abs((int)a[k] - (int)d[k]);
                if (e1) mm_avx_sse++;
                if (e2) mm_avx_def++;
                if (e1 > max_avx_sse) max_avx_sse = e1;
                if (e2 > max_avx_def) max_avx_def = e2;
            }
        }
    }
    printf("pixels        = %ld\n", total);
    printf("AVX checksum  = %llu\n", avx_sum);
    printf("AVX vs SSE    : mismatches=%ld (%.3f%%) maxdiff=%d\n",
           mm_avx_sse, 100.0 * mm_avx_sse / total, max_avx_sse);
    printf("AVX vs scalar : mismatches=%ld (%.3f%%) maxdiff=%d\n",
           mm_avx_def, 100.0 * mm_avx_def / total, max_avx_def);

    // ---- single-precision 8-wide path vs scalar, at this (shallow) scale ----
    long ftotal = 0, mm_flt = 0;
    int max_flt = 0;
    unsigned long long flt_sum = 0;
    for (int j = 0; j < H; j++) {
        double y = ymin + j * (ymax - ymin) / H;
        for (int i = 0; i < W; i += 8) {       // W divisible by 8
            double x = xmin + i * xstep;
            unsigned char f[8], d[8];
            unsigned char *pf = f, *pd = d;
            CoreLoopFloatAVX(x, y, xstep, &pf);
            CoreLoopDoubleDefault(x,            y, xstep, &pd);
            CoreLoopDoubleDefault(x + 4*xstep,  y, xstep, &pd);
            for (int k = 0; k < 8; k++) {
                ftotal++;
                flt_sum += f[k];
                int e = abs((int)f[k] - (int)d[k]);
                if (e) mm_flt++;
                if (e > max_flt) max_flt = e;
            }
        }
    }
    printf("FLOAT checksum= %llu\n", flt_sum);
    printf("FLOAT vs scalar: mismatches=%ld (%.3f%%) maxdiff=%d\n",
           mm_flt, 100.0 * mm_flt / ftotal, max_flt);

    // ---- throughput: float 8-wide vs double-AVX 4-wide over the grid ----
    using clk = std::chrono::high_resolution_clock;
    const int REP = 20;
    volatile unsigned long long sink = 0;
    unsigned char buf[8];

    auto t0 = clk::now();
    for (int r = 0; r < REP; r++)
      for (int jj = 0; jj < H; jj++) {
        double y = ymin + jj * (ymax - ymin) / H;
        for (int i = 0; i < W; i += 4) {
            double x = xmin + i * xstep;
            unsigned char *pd = buf;
            CoreLoopDoubleAVX(x, y, xstep, &pd);
            sink += buf[0];
        }
      }
    auto t1 = clk::now();
    for (int r = 0; r < REP; r++)
      for (int jj = 0; jj < H; jj++) {
        double y = ymin + jj * (ymax - ymin) / H;
        for (int i = 0; i < W; i += 8) {
            double x = xmin + i * xstep;
            unsigned char *pf = buf;
            CoreLoopFloatAVX(x, y, xstep, &pf);
            sink += buf[0];
        }
      }
    auto t2 = clk::now();
    double ms_d = std::chrono::duration<double,std::milli>(t1-t0).count();
    double ms_f = std::chrono::duration<double,std::milli>(t2-t1).count();
    printf("throughput (%d reps over the shallow grid):\n", REP);
    printf("  double-AVX (4-wide) : %.1f ms\n", ms_d);
    printf("  float-AVX  (8-wide) : %.1f ms  (%.2fx)\n", ms_f, ms_d/ms_f);
    (void)sink;

    // ---- pass/fail bounds (catch a broken SIMD core loop) ----
    // The SIMD loops differ from scalar only at the chaotic boundary, due to
    // the periodicity approximation and (for float) reduced precision. A real
    // register/operand bug would move thousands of pixels, far past these.
    int rc = 0;
    double p_avx_def = 100.0 * mm_avx_def / total;
    double p_avx_sse = 100.0 * mm_avx_sse / total;
    double p_flt     = 100.0 * mm_flt     / ftotal;
    if (p_avx_def > 0.1) { printf("FAIL: AVX vs scalar %.3f%% > 0.1%%\n", p_avx_def); rc = 1; }
    if (p_avx_sse > 0.5) { printf("FAIL: AVX vs SSE %.3f%% > 0.5%%\n", p_avx_sse); rc = 1; }
    if (p_flt     > 1.0) { printf("FAIL: FLOAT vs scalar %.3f%% > 1.0%%\n", p_flt); rc = 1; }
    printf(rc ? "RESULT: FAIL\n" : "RESULT: PASS\n");
    return rc;
}
