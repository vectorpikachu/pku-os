#ifndef THREADS_FIXED_POINT_H
#define THREADS_FIXED_POINT_H
/**
 * This file contains the fixed-point arithmetic implementation.
 * fixed-point.h is a new file added by the reference solution.
 */

#define INT_BITS 17
#define FRAC_BITS 14
#define F (1 << FRAC_BITS)

/* Conversions */
#define INT_TO_FP(n) ((n) * (F))
#define FP_TO_INT_ZERO(x) ((x) / (F))
#define FP_TO_INT_NEAREST(x) (((x) >= 0) ? ((x) + (F) / 2) / (F) : ((x) - (F) / 2) / (F))

/* Arithmetic */
#define FP_ADD(x, y) ((x) + (y))
#define FP_SUB(x, y) ((x) - (y))
#define FP_ADD_INT(x, n) ((x) + (n) * (F))
#define FP_SUB_INT(x, n) ((x) - (n) * (F))
#define FP_MUL(x, y) (((int64_t)(x)) * (y) / (F))
#define FP_MUL_INT(x, n) ((x) * (n))
#define FP_DIV(x, y) (((int64_t)(x)) * (F) / (y))
#define FP_DIV_INT(x, n) ((x) / (n))

#endif