/* Resampling library
 * Copyright (C) <2001> David Schleef <ds@schleef.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */


#ifndef __RESAMPLE_H__
#define __RESAMPLE_H__

#include <config.h>

typedef struct resample_s resample_t;

struct resample_s {
	/* parameters */

	int method;
	int channels;
	int verbose;

	int filter_length;

	double i_rate;
	double o_rate;

	void *priv;

	void *(*get_buffer)(void *priv, unsigned int size);

	/* internal parameters */

	double halftaps;

	/* filter state */

	void *buffer;
	int buffer_len;

	double i_start;
	double o_start;

	double i_start_buf;
	double i_end_buf;

	double i_inc;
	double o_inc;

	double i_end;
	double o_end;

	int i_samples;
	int o_samples;

	void *i_buf, *o_buf;

	double acc[10];

	/* methods */
	void (*scale)(resample_t *r);

	double ack;
};

enum{
	RESAMPLE_NEAREST = 0,
	RESAMPLE_BILINEAR,
	RESAMPLE_SINC_SLOW,
	RESAMPLE_SINC,
};

void resample_init(resample_t *r);
void resample_reinit(resample_t *r);

void resample_scale(resample_t *r, void *i_buf, unsigned int size);

void resample_nearest(resample_t *r);
void resample_bilinear(resample_t *r);
void resample_sinc(resample_t *r);
void resample_sinc_slow(resample_t *r);


typedef struct functable_s functable_t;
struct functable_s {
	double start;
	double offset;
	int len;

	double invoffset;

	double scale;
	double scale2;

	double (*func_x)(void *,double x);
	double (*func_dx)(void *,double x);

	double (*func2_x)(void *,double x);
	double (*func2_dx)(void *,double x);

	double *fx;
	double *fdx;

	void *priv;
};

void functable_init(functable_t *t);
double functable_eval(functable_t *t,double x);

double functable_fir(functable_t *t,double x0,int n,double *data,int len);
void functable_fir2(functable_t *t,double *r0, double *r1, double x0,
	int n,double *data,int len);

double functable_sinc(void *p, double x);
double functable_dsinc(void *p, double x);
double functable_window_std(void *p, double x);
double functable_window_dstd(void *p, double x);
double functable_window_boxcar(void *p, double x);
double functable_window_dboxcar(void *p, double x);

/* math lib stuff */

void conv_double_short_table(double *dest, short *src, int n);
void conv_double_short_unroll(double *dest, short *src, int n);
void conv_double_short_ref(double *dest, short *src, int n);
#ifdef HAVE_CPU_PPC
void conv_double_short_altivec(double *dest, short *src, int n);
#endif

void conv_short_double_ref(short *dest, double *src, int n);
#ifdef HAVE_CPU_PPC
void conv_short_double_ppcasm(short *dest, double *src, int n);
#endif

#ifdef HAVE_CPU_PPC
#define conv_double_short conv_double_short_table
#define conv_short_double conv_short_double_ppcasm
#else
#define conv_double_short conv_double_short_ref
#define conv_short_double conv_short_double_ref
#endif

void conv_double_short_dstr(double *dest, short *src, int n, int dstr);
void conv_short_double_sstr(short *dest, double *src, int n, int dstr);

#endif /* __RESAMPLE_H__ */

