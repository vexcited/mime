#ifndef MIME_LIB_ICC_INTERNAL_H
#define MIME_LIB_ICC_INTERNAL_H

#include "icc.h"

#define memcpy __builtin_memcpy
#define memmove __builtin_memmove
#define memset __builtin_memset
#define memcmp __builtin_memcmp

extern const double icc_d50[3];

double icc_log(double x);
double icc_exp(double x);
double icc_s15(const uint8_t *p);
void icc_put_s15(uint8_t *p, double v);
bool icc_mat_invert(const double m[9], double out[9]);
void icc_mat_mul(const double a[9], const double b[9], double out[9]);
void icc_mat_apply(const double m[9], const double in[3], double out[3]);
/* the bradford matrix that adapts colors seen under one white to another. */
void icc_bradford(const double from[3], const double to[3], double out[9]);

/* a curve of the engine: a parametric function in the general form of type 4, or a table. */
enum { CURVE_IDENTITY, CURVE_PARAMETRIC, CURVE_TABLE };
typedef struct IccCurve {
    uint8_t kind;
    bool inverse;
    /* g, a, b, c, d, e, f: (a x + b)^g + e from d up, c x + f below. */
    double p[7];
    uint32_t n;
    float *table;
} IccCurve;

double icc_curve_eval(const IccCurve *c, double x);

#endif
