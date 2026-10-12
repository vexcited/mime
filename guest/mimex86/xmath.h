/*
 * elementary functions for the x87, see xmath.c.
 */
#ifndef MIME_X86_XMATH_H
#define MIME_X86_XMATH_H

#include <stdint.h>

double xm_scalbn(double x, int n);
double xm_frexp(double x, int *e);
double xm_fmod(double x, double y, uint64_t *quotient);
double xm_sin(double x);
double xm_cos(double x);
double xm_tan(double x);
double xm_atan2(double y, double x);
double xm_log2(double x);
double xm_exp2(double x);
double xm_exp2m1(double x);
double xm_log2p1(double x);

#endif
