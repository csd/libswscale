/*
    Copyright (C) 2001-2002 Michael Niedermayer <michaelni@gmx.at>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

/*
  supported Input formats: YV12 (grayscale soon too)
  supported output formats: YV12, BGR15, BGR16, BGR24, BGR32 (grayscale soon too)
*/

#include <inttypes.h>
#include <string.h>
#include <math.h>
#include <stdio.h>
#include "../config.h"
#include "../mangle.h"
#ifdef HAVE_MALLOC_H
#include <malloc.h>
#endif
#include "swscale.h"
#include "../cpudetect.h"
#include "../libvo/img_format.h"
#undef MOVNTQ
#undef PAVGB

//#undef HAVE_MMX2
//#define HAVE_3DNOW
//#undef HAVE_MMX
//#undef ARCH_X86
#define DITHER1XBPP

#define RET 0xC3 //near return opcode

#ifdef MP_DEBUG
#define ASSERT(x) if(!(x)) { printf("ASSERT " #x " failed\n"); *((int*)0)=0; }
#else
#define ASSERT(x) ;
#endif

#ifdef M_PI
#define PI M_PI
#else
#define PI 3.14159265358979323846
#endif

extern int verbose; // defined in mplayer.c
/*
NOTES

known BUGS with known cause (no bugreports please!, but patches are welcome :) )
horizontal fast_bilinear MMX2 scaler reads 1-7 samples too much (might cause a sig11)

Supported output formats BGR15 BGR16 BGR24 BGR32 YV12
BGR15 & BGR16 MMX verions support dithering
Special versions: fast Y 1:1 scaling (no interpolation in y direction)

TODO
more intelligent missalignment avoidance for the horizontal scaler
change the distance of the u & v buffer
write special vertical cubic upscale version
Optimize C code (yv12 / minmax)
add support for packed pixel yuv input & output
add support for Y8 input & output
add BGR4 output support
add BGR32 / BGR24 input support
*/

#define ABS(a) ((a) > 0 ? (a) : (-(a)))
#define MIN(a,b) ((a) > (b) ? (b) : (a))
#define MAX(a,b) ((a) < (b) ? (b) : (a))

#ifdef ARCH_X86
#define CAN_COMPILE_X86_ASM
#endif

#ifdef CAN_COMPILE_X86_ASM
static uint64_t __attribute__((aligned(8))) yCoeff=    0x2568256825682568LL;
static uint64_t __attribute__((aligned(8))) vrCoeff=   0x3343334333433343LL;
static uint64_t __attribute__((aligned(8))) ubCoeff=   0x40cf40cf40cf40cfLL;
static uint64_t __attribute__((aligned(8))) vgCoeff=   0xE5E2E5E2E5E2E5E2LL;
static uint64_t __attribute__((aligned(8))) ugCoeff=   0xF36EF36EF36EF36ELL;
static uint64_t __attribute__((aligned(8))) bF8=       0xF8F8F8F8F8F8F8F8LL;
static uint64_t __attribute__((aligned(8))) bFC=       0xFCFCFCFCFCFCFCFCLL;
static uint64_t __attribute__((aligned(8))) w400=      0x0400040004000400LL;
static uint64_t __attribute__((aligned(8))) w80=       0x0080008000800080LL;
static uint64_t __attribute__((aligned(8))) w10=       0x0010001000100010LL;
static uint64_t __attribute__((aligned(8))) w02=       0x0002000200020002LL;
static uint64_t __attribute__((aligned(8))) bm00001111=0x00000000FFFFFFFFLL;
static uint64_t __attribute__((aligned(8))) bm00000111=0x0000000000FFFFFFLL;
static uint64_t __attribute__((aligned(8))) bm11111000=0xFFFFFFFFFF000000LL;

static volatile uint64_t __attribute__((aligned(8))) b5Dither;
static volatile uint64_t __attribute__((aligned(8))) g5Dither;
static volatile uint64_t __attribute__((aligned(8))) g6Dither;
static volatile uint64_t __attribute__((aligned(8))) r5Dither;

static uint64_t __attribute__((aligned(8))) dither4[2]={
	0x0103010301030103LL,
	0x0200020002000200LL,};

static uint64_t __attribute__((aligned(8))) dither8[2]={
	0x0602060206020602LL,
	0x0004000400040004LL,};

static uint64_t __attribute__((aligned(8))) b16Mask=   0x001F001F001F001FLL;
static uint64_t __attribute__((aligned(8))) g16Mask=   0x07E007E007E007E0LL;
static uint64_t __attribute__((aligned(8))) r16Mask=   0xF800F800F800F800LL;
static uint64_t __attribute__((aligned(8))) b15Mask=   0x001F001F001F001FLL;
static uint64_t __attribute__((aligned(8))) g15Mask=   0x03E003E003E003E0LL;
static uint64_t __attribute__((aligned(8))) r15Mask=   0x7C007C007C007C00LL;

static uint64_t __attribute__((aligned(8))) M24A=   0x00FF0000FF0000FFLL;
static uint64_t __attribute__((aligned(8))) M24B=   0xFF0000FF0000FF00LL;
static uint64_t __attribute__((aligned(8))) M24C=   0x0000FF0000FF0000LL;

// FIXME remove
static uint64_t __attribute__((aligned(8))) asm_yalpha1;
static uint64_t __attribute__((aligned(8))) asm_uvalpha1;
#endif

// clipping helper table for C implementations:
static unsigned char clip_table[768];

static unsigned short clip_table16b[768];
static unsigned short clip_table16g[768];
static unsigned short clip_table16r[768];
static unsigned short clip_table15b[768];
static unsigned short clip_table15g[768];
static unsigned short clip_table15r[768];

// yuv->rgb conversion tables:
static    int yuvtab_2568[256];
static    int yuvtab_3343[256];
static    int yuvtab_0c92[256];
static    int yuvtab_1a1e[256];
static    int yuvtab_40cf[256];
// Needed for cubic scaler to catch overflows
static    int clip_yuvtab_2568[768];
static    int clip_yuvtab_3343[768];
static    int clip_yuvtab_0c92[768];
static    int clip_yuvtab_1a1e[768];
static    int clip_yuvtab_40cf[768];

//global sws_flags from the command line
int sws_flags=0;

//global srcFilter
SwsFilter src_filter= {NULL, NULL, NULL, NULL};

float sws_lum_gblur= 0.0;
float sws_chr_gblur= 0.0;
int sws_chr_vshift= 0;
int sws_chr_hshift= 0;
float sws_chr_sharpen= 0.0;
float sws_lum_sharpen= 0.0;

/* cpuCaps combined from cpudetect and whats actually compiled in
   (if there is no support for something compiled in it wont appear here) */
static CpuCaps cpuCaps;

void (*swScale)(SwsContext *context, uint8_t* src[], int srcStride[], int srcSliceY,
             int srcSliceH, uint8_t* dst[], int dstStride[])=NULL;

static SwsVector *getConvVec(SwsVector *a, SwsVector *b);

#ifdef CAN_COMPILE_X86_ASM
void in_asm_used_var_warning_killer()
{
 volatile int i= yCoeff+vrCoeff+ubCoeff+vgCoeff+ugCoeff+bF8+bFC+w400+w80+w10+
 bm00001111+bm00000111+bm11111000+b16Mask+g16Mask+r16Mask+b15Mask+g15Mask+r15Mask+asm_yalpha1+ asm_uvalpha1+
 M24A+M24B+M24C+w02 + b5Dither+g5Dither+r5Dither+g6Dither+dither4[0]+dither8[0];
 if(i) i=0;
}
#endif

static inline void yuv2yuvXinC(int16_t *lumFilter, int16_t **lumSrc, int lumFilterSize,
				    int16_t *chrFilter, int16_t **chrSrc, int chrFilterSize,
				    uint8_t *dest, uint8_t *uDest, uint8_t *vDest, int dstW)
{
	//FIXME Optimize (just quickly writen not opti..)
	int i;
	for(i=0; i<dstW; i++)
	{
		int val=0;
		int j;
		for(j=0; j<lumFilterSize; j++)
			val += lumSrc[j][i] * lumFilter[j];

		dest[i]= MIN(MAX(val>>19, 0), 255);
	}

	if(uDest != NULL)
		for(i=0; i<(dstW>>1); i++)
		{
			int u=0;
			int v=0;
			int j;
			for(j=0; j<chrFilterSize; j++)
			{
				u += chrSrc[j][i] * chrFilter[j];
				v += chrSrc[j][i + 2048] * chrFilter[j];
			}

			uDest[i]= MIN(MAX(u>>19, 0), 255);
			vDest[i]= MIN(MAX(v>>19, 0), 255);
		}
}

static inline void yuv2rgbXinC(int16_t *lumFilter, int16_t **lumSrc, int lumFilterSize,
				    int16_t *chrFilter, int16_t **chrSrc, int chrFilterSize,
				    uint8_t *dest, int dstW, int dstFormat)
{
	if(dstFormat==IMGFMT_BGR32)
	{
		int i;
		for(i=0; i<(dstW>>1); i++){
			int j;
			int Y1=0;
			int Y2=0;
			int U=0;
			int V=0;
			int Cb, Cr, Cg;
			for(j=0; j<lumFilterSize; j++)
			{
				Y1 += lumSrc[j][2*i] * lumFilter[j];
				Y2 += lumSrc[j][2*i+1] * lumFilter[j];
			}
			for(j=0; j<chrFilterSize; j++)
			{
				U += chrSrc[j][i] * chrFilter[j];
				V += chrSrc[j][i+2048] * chrFilter[j];
			}
			Y1= clip_yuvtab_2568[ (Y1>>19) + 256 ];
			Y2= clip_yuvtab_2568[ (Y2>>19) + 256 ];
			U >>= 19;
			V >>= 19;

			Cb= clip_yuvtab_40cf[U+ 256];
			Cg= clip_yuvtab_1a1e[V+ 256] + yuvtab_0c92[U+ 256];
			Cr= clip_yuvtab_3343[V+ 256];

			dest[8*i+0]=clip_table[((Y1 + Cb) >>13)];
			dest[8*i+1]=clip_table[((Y1 + Cg) >>13)];
			dest[8*i+2]=clip_table[((Y1 + Cr) >>13)];

			dest[8*i+4]=clip_table[((Y2 + Cb) >>13)];
			dest[8*i+5]=clip_table[((Y2 + Cg) >>13)];
			dest[8*i+6]=clip_table[((Y2 + Cr) >>13)];
		}
	}
	else if(dstFormat==IMGFMT_BGR24)
	{
		int i;
		for(i=0; i<(dstW>>1); i++){
			int j;
			int Y1=0;
			int Y2=0;
			int U=0;
			int V=0;
			int Cb, Cr, Cg;
			for(j=0; j<lumFilterSize; j++)
			{
				Y1 += lumSrc[j][2*i] * lumFilter[j];
				Y2 += lumSrc[j][2*i+1] * lumFilter[j];
			}
			for(j=0; j<chrFilterSize; j++)
			{
				U += chrSrc[j][i] * chrFilter[j];
				V += chrSrc[j][i+2048] * chrFilter[j];
			}
			Y1= clip_yuvtab_2568[ (Y1>>19) + 256 ];
			Y2= clip_yuvtab_2568[ (Y2>>19) + 256 ];
			U >>= 19;
			V >>= 19;

			Cb= clip_yuvtab_40cf[U+ 256];
			Cg= clip_yuvtab_1a1e[V+ 256] + yuvtab_0c92[U+ 256];
			Cr= clip_yuvtab_3343[V+ 256];

			dest[0]=clip_table[((Y1 + Cb) >>13)];
			dest[1]=clip_table[((Y1 + Cg) >>13)];
			dest[2]=clip_table[((Y1 + Cr) >>13)];

			dest[3]=clip_table[((Y2 + Cb) >>13)];
			dest[4]=clip_table[((Y2 + Cg) >>13)];
			dest[5]=clip_table[((Y2 + Cr) >>13)];
			dest+=6;
		}
	}
	else if(dstFormat==IMGFMT_BGR16)
	{
		int i;
#ifdef DITHER1XBPP
		static int ditherb1=1<<14;
		static int ditherg1=1<<13;
		static int ditherr1=2<<14;
		static int ditherb2=3<<14;
		static int ditherg2=3<<13;
		static int ditherr2=0<<14;

		ditherb1 ^= (1^2)<<14;
		ditherg1 ^= (1^2)<<13;
		ditherr1 ^= (1^2)<<14;
		ditherb2 ^= (3^0)<<14;
		ditherg2 ^= (3^0)<<13;
		ditherr2 ^= (3^0)<<14;
#else
		const int ditherb1=0;
		const int ditherg1=0;
		const int ditherr1=0;
		const int ditherb2=0;
		const int ditherg2=0;
		const int ditherr2=0;
#endif
		for(i=0; i<(dstW>>1); i++){
			int j;
			int Y1=0;
			int Y2=0;
			int U=0;
			int V=0;
			int Cb, Cr, Cg;
			for(j=0; j<lumFilterSize; j++)
			{
				Y1 += lumSrc[j][2*i] * lumFilter[j];
				Y2 += lumSrc[j][2*i+1] * lumFilter[j];
			}
			for(j=0; j<chrFilterSize; j++)
			{
				U += chrSrc[j][i] * chrFilter[j];
				V += chrSrc[j][i+2048] * chrFilter[j];
			}
			Y1= clip_yuvtab_2568[ (Y1>>19) + 256 ];
			Y2= clip_yuvtab_2568[ (Y2>>19) + 256 ];
			U >>= 19;
			V >>= 19;

			Cb= clip_yuvtab_40cf[U+ 256];
			Cg= clip_yuvtab_1a1e[V+ 256] + yuvtab_0c92[U+ 256];
			Cr= clip_yuvtab_3343[V+ 256];

			((uint16_t*)dest)[2*i] =
				clip_table16b[(Y1 + Cb + ditherb1) >>13] |
				clip_table16g[(Y1 + Cg + ditherg1) >>13] |
				clip_table16r[(Y1 + Cr + ditherr1) >>13];

			((uint16_t*)dest)[2*i+1] =
				clip_table16b[(Y2 + Cb + ditherb2) >>13] |
				clip_table16g[(Y2 + Cg + ditherg2) >>13] |
				clip_table16r[(Y2 + Cr + ditherr2) >>13];
		}
	}
	else if(dstFormat==IMGFMT_BGR15)
	{
		int i;
#ifdef DITHER1XBPP
		static int ditherb1=1<<14;
		static int ditherg1=1<<14;
		static int ditherr1=2<<14;
		static int ditherb2=3<<14;
		static int ditherg2=3<<14;
		static int ditherr2=0<<14;

		ditherb1 ^= (1^2)<<14;
		ditherg1 ^= (1^2)<<14;
		ditherr1 ^= (1^2)<<14;
		ditherb2 ^= (3^0)<<14;
		ditherg2 ^= (3^0)<<14;
		ditherr2 ^= (3^0)<<14;
#else
		const int ditherb1=0;
		const int ditherg1=0;
		const int ditherr1=0;
		const int ditherb2=0;
		const int ditherg2=0;
		const int ditherr2=0;
#endif
		for(i=0; i<(dstW>>1); i++){
			int j;
			int Y1=0;
			int Y2=0;
			int U=0;
			int V=0;
			int Cb, Cr, Cg;
			for(j=0; j<lumFilterSize; j++)
			{
				Y1 += lumSrc[j][2*i] * lumFilter[j];
				Y2 += lumSrc[j][2*i+1] * lumFilter[j];
			}
			for(j=0; j<chrFilterSize; j++)
			{
				U += chrSrc[j][i] * chrFilter[j];
				V += chrSrc[j][i+2048] * chrFilter[j];
			}
			Y1= clip_yuvtab_2568[ (Y1>>19) + 256 ];
			Y2= clip_yuvtab_2568[ (Y2>>19) + 256 ];
			U >>= 19;
			V >>= 19;

			Cb= clip_yuvtab_40cf[U+ 256];
			Cg= clip_yuvtab_1a1e[V+ 256] + yuvtab_0c92[U+ 256];
			Cr= clip_yuvtab_3343[V+ 256];

			((uint16_t*)dest)[2*i] =
				clip_table15b[(Y1 + Cb + ditherb1) >>13] |
				clip_table15g[(Y1 + Cg + ditherg1) >>13] |
				clip_table15r[(Y1 + Cr + ditherr1) >>13];

			((uint16_t*)dest)[2*i+1] =
				clip_table15b[(Y2 + Cb + ditherb2) >>13] |
				clip_table15g[(Y2 + Cg + ditherg2) >>13] |
				clip_table15r[(Y2 + Cr + ditherr2) >>13];
		}
	}
}


//Note: we have C, X86, MMX, MMX2, 3DNOW version therse no 3DNOW+MMX2 one
//Plain C versions
#if !defined (HAVE_MMX) || defined (RUNTIME_CPUDETECT)
#define COMPILE_C
#endif

#ifdef CAN_COMPILE_X86_ASM

#if (defined (HAVE_MMX) && !defined (HAVE_3DNOW) && !defined (HAVE_MMX2)) || defined (RUNTIME_CPUDETECT)
#define COMPILE_MMX
#endif

#if defined (HAVE_MMX2) || defined (RUNTIME_CPUDETECT)
#define COMPILE_MMX2
#endif

#if (defined (HAVE_3DNOW) && !defined (HAVE_MMX2)) || defined (RUNTIME_CPUDETECT)
#define COMPILE_3DNOW
#endif
#endif //CAN_COMPILE_X86_ASM

#undef HAVE_MMX
#undef HAVE_MMX2
#undef HAVE_3DNOW

#ifdef COMPILE_C
#undef HAVE_MMX
#undef HAVE_MMX2
#undef HAVE_3DNOW
#define RENAME(a) a ## _C
#include "swscale_template.c"
#endif

#ifdef CAN_COMPILE_X86_ASM

//X86 versions
/*
#undef RENAME
#undef HAVE_MMX
#undef HAVE_MMX2
#undef HAVE_3DNOW
#define ARCH_X86
#define RENAME(a) a ## _X86
#include "swscale_template.c"
*/
//MMX versions
#ifdef COMPILE_MMX
#undef RENAME
#define HAVE_MMX
#undef HAVE_MMX2
#undef HAVE_3DNOW
#define RENAME(a) a ## _MMX
#include "swscale_template.c"
#endif

//MMX2 versions
#ifdef COMPILE_MMX2
#undef RENAME
#define HAVE_MMX
#define HAVE_MMX2
#undef HAVE_3DNOW
#define RENAME(a) a ## _MMX2
#include "swscale_template.c"
#endif

//3DNOW versions
#ifdef COMPILE_3DNOW
#undef RENAME
#define HAVE_MMX
#undef HAVE_MMX2
#define HAVE_3DNOW
#define RENAME(a) a ## _3DNow
#include "swscale_template.c"
#endif

#endif //CAN_COMPILE_X86_ASM

// minor note: the HAVE_xyz is messed up after that line so dont use it


// old global scaler, dont use for new code, unless it uses only the stuff from the command line
// will use sws_flags from the command line
void SwScale_YV12slice(unsigned char* src[], int srcStride[], int srcSliceY ,
			     int srcSliceH, uint8_t* dst[], int dstStride, int dstbpp,
			     int srcW, int srcH, int dstW, int dstH){

	static SwsContext *context=NULL;
	int dstFormat;
	int flags=0;
	static int firstTime=1;
	int dstStride3[3]= {dstStride, dstStride>>1, dstStride>>1};

	if(firstTime)
	{
#ifdef ARCH_X86
		if(gCpuCaps.hasMMX)
			asm volatile("emms\n\t"::: "memory"); //FIXME this shouldnt be required but it IS (even for non mmx versions)
#endif
		flags= SWS_PRINT_INFO;
		firstTime=0;

		if(src_filter.lumH) freeVec(src_filter.lumH);
		if(src_filter.lumV) freeVec(src_filter.lumV);
		if(src_filter.chrH) freeVec(src_filter.chrH);
		if(src_filter.chrV) freeVec(src_filter.chrV);

		if(sws_lum_gblur!=0.0){
			src_filter.lumH= getGaussianVec(sws_lum_gblur, 3.0);
			src_filter.lumV= getGaussianVec(sws_lum_gblur, 3.0);
		}else{
			src_filter.lumH= getIdentityVec();
			src_filter.lumV= getIdentityVec();
		}

		if(sws_chr_gblur!=0.0){
			src_filter.chrH= getGaussianVec(sws_chr_gblur, 3.0);
			src_filter.chrV= getGaussianVec(sws_chr_gblur, 3.0);
		}else{
			src_filter.chrH= getIdentityVec();
			src_filter.chrV= getIdentityVec();
		}

		if(sws_chr_sharpen!=0.0){
			SwsVector *g= getConstVec(-1.0, 3);
			SwsVector *id= getConstVec(10.0/sws_chr_sharpen, 1);
			g->coeff[1]=2.0;
			addVec(id, g);
			convVec(src_filter.chrH, id);
			convVec(src_filter.chrV, id);
			freeVec(g);
			freeVec(id);
		}

		if(sws_lum_sharpen!=0.0){
			SwsVector *g= getConstVec(-1.0, 3);
			SwsVector *id= getConstVec(10.0/sws_lum_sharpen, 1);
			g->coeff[1]=2.0;
			addVec(id, g);
			convVec(src_filter.lumH, id);
			convVec(src_filter.lumV, id);
			freeVec(g);
			freeVec(id);
		}

		if(sws_chr_hshift)
			shiftVec(src_filter.chrH, sws_chr_hshift);

		if(sws_chr_vshift)
			shiftVec(src_filter.chrV, sws_chr_vshift);

		normalizeVec(src_filter.chrH, 1.0);
		normalizeVec(src_filter.chrV, 1.0);
		normalizeVec(src_filter.lumH, 1.0);
		normalizeVec(src_filter.lumV, 1.0);

		if(verbose > 1) printVec(src_filter.chrH);
		if(verbose > 1) printVec(src_filter.lumH);
	}

	switch(dstbpp)
	{
		case 8 : dstFormat= IMGFMT_Y8;		break;
		case 12: dstFormat= IMGFMT_YV12;	break;
		case 15: dstFormat= IMGFMT_BGR15;	break;
		case 16: dstFormat= IMGFMT_BGR16;	break;
		case 24: dstFormat= IMGFMT_BGR24;	break;
		case 32: dstFormat= IMGFMT_BGR32;	break;
		default: return;
	}

	switch(sws_flags)
	{
		case 0: flags|= SWS_FAST_BILINEAR; break;
		case 1: flags|= SWS_BILINEAR; break;
		case 2: flags|= SWS_BICUBIC; break;
		case 3: flags|= SWS_X; break;
		case 4: flags|= SWS_POINT; break;
		default:flags|= SWS_BILINEAR; break;
	}

	if(!context) context=getSwsContext(srcW, srcH, IMGFMT_YV12, dstW, dstH, dstFormat, flags, &src_filter, NULL);


	swScale(context, src, srcStride, srcSliceY, srcSliceH, dst, dstStride3);
}

static inline void initFilter(int16_t **outFilter, int16_t **filterPos, int *outFilterSize, int xInc,
			      int srcW, int dstW, int filterAlign, int one, int flags,
			      SwsVector *srcFilter, SwsVector *dstFilter)
{
	int i;
	int filterSize;
	int filter2Size;
	int minFilterSize;
	double *filter=NULL;
	double *filter2=NULL;
#ifdef ARCH_X86
	if(gCpuCaps.hasMMX)
		asm volatile("emms\n\t"::: "memory"); //FIXME this shouldnt be required but it IS (even for non mmx versions)
#endif

	*filterPos = (int16_t*)memalign(8, dstW*sizeof(int16_t));
	if(ABS(xInc - 0x10000) <10) // unscaled
	{
		int i;
		filterSize= 1;
		filter= (double*)memalign(8, dstW*sizeof(double)*filterSize);
		for(i=0; i<dstW*filterSize; i++) filter[i]=0;

		for(i=0; i<dstW; i++)
		{
			filter[i*filterSize]=1;
			(*filterPos)[i]=i;
		}

	}
	else if(flags&SWS_POINT) // lame looking point sampling mode
	{
		int i;
		int xDstInSrc;
		filterSize= 1;
		filter= (double*)memalign(8, dstW*sizeof(double)*filterSize);
		
		xDstInSrc= xInc/2 - 0x8000;
		for(i=0; i<dstW; i++)
		{
			int xx= (xDstInSrc>>16) - (filterSize>>1) + 1;

			(*filterPos)[i]= xx;
			filter[i]= 1.0;
			xDstInSrc+= xInc;
		}
	}
	else if(xInc <= (1<<16) || (flags&SWS_FAST_BILINEAR)) // upscale
	{
		int i;
		int xDstInSrc;
		if     (flags&SWS_BICUBIC) filterSize= 4;
		else if(flags&SWS_X      ) filterSize= 4;
		else			   filterSize= 2;
//		printf("%d %d %d\n", filterSize, srcW, dstW);
		filter= (double*)memalign(8, dstW*sizeof(double)*filterSize);

		xDstInSrc= xInc/2 - 0x8000;
		for(i=0; i<dstW; i++)
		{
			int xx= (xDstInSrc>>16) - (filterSize>>1) + 1;
			int j;

			(*filterPos)[i]= xx;
			if((flags & SWS_BICUBIC) || (flags & SWS_X))
			{
				double d= ABS(((xx+1)<<16) - xDstInSrc)/(double)(1<<16);
				double y1,y2,y3,y4;
				double A= -0.6;
				if(flags & SWS_BICUBIC){
						// Equation is from VirtualDub
					y1 = (        +     A*d -       2.0*A*d*d +       A*d*d*d);
					y2 = (+ 1.0             -     (A+3.0)*d*d + (A+2.0)*d*d*d);
					y3 = (        -     A*d + (2.0*A+3.0)*d*d - (A+2.0)*d*d*d);
					y4 = (                  +           A*d*d -       A*d*d*d);
				}else{
						// cubic interpolation (derived it myself)
					y1 = (    -2.0*d + 3.0*d*d - 1.0*d*d*d)/6.0;
					y2 = (6.0 -3.0*d - 6.0*d*d + 3.0*d*d*d)/6.0;
					y3 = (    +6.0*d + 3.0*d*d - 3.0*d*d*d)/6.0;
					y4 = (    -1.0*d           + 1.0*d*d*d)/6.0;
				}

//				printf("%d %d %d \n", coeff, (int)d, xDstInSrc);
				filter[i*filterSize + 0]= y1;
				filter[i*filterSize + 1]= y2;
				filter[i*filterSize + 2]= y3;
				filter[i*filterSize + 3]= y4;
//				printf("%1.3f %1.3f %1.3f %1.3f %1.3f\n",d , y1, y2, y3, y4);
			}
			else
			{
				for(j=0; j<filterSize; j++)
				{
					double d= ABS((xx<<16) - xDstInSrc)/(double)(1<<16);
					double coeff= 1.0 - d;
					if(coeff<0) coeff=0;
	//				printf("%d %d %d \n", coeff, (int)d, xDstInSrc);
					filter[i*filterSize + j]= coeff;
					xx++;
				}
			}
			xDstInSrc+= xInc;
		}
	}
	else // downscale
	{
		int xDstInSrc;
		if(flags&SWS_BICUBIC) filterSize= (int)ceil(1 + 4.0*srcW / (double)dstW);
		else if(flags&SWS_X)  filterSize= (int)ceil(1 + 4.0*srcW / (double)dstW);
		else		      filterSize= (int)ceil(1 + 2.0*srcW / (double)dstW);
//		printf("%d %d %d\n", *filterSize, srcW, dstW);
		filter= (double*)memalign(8, dstW*sizeof(double)*filterSize);

		xDstInSrc= xInc/2 - 0x8000;
		for(i=0; i<dstW; i++)
		{
			int xx= (int)((double)xDstInSrc/(double)(1<<16) - (filterSize-1)*0.5 + 0.5);
			int j;
			(*filterPos)[i]= xx;
			for(j=0; j<filterSize; j++)
			{
				double d= ABS((xx<<16) - xDstInSrc)/(double)xInc;
				double coeff;
				if((flags & SWS_BICUBIC) || (flags & SWS_X))
				{
					double A= -0.75;
//					d*=2;
					// Equation is from VirtualDub
					if(d<1.0)
						coeff = (1.0 - (A+3.0)*d*d + (A+2.0)*d*d*d);
					else if(d<2.0)
						coeff = (-4.0*A + 8.0*A*d - 5.0*A*d*d + A*d*d*d);
					else
						coeff=0.0;
				}
/*				else if(flags & SWS_X)
				{
				}*/
				else
				{
					coeff= 1.0 - d;
					if(coeff<0) coeff=0;
				}
//				printf("%1.3f %d %d \n", coeff, (int)d, xDstInSrc);
				filter[i*filterSize + j]= coeff;
				xx++;
			}
			xDstInSrc+= xInc;
		}
	}

	/* apply src & dst Filter to filter -> filter2
	   free(filter);
	*/
	filter2Size= filterSize;
	if(srcFilter) filter2Size+= srcFilter->length - 1;
	if(dstFilter) filter2Size+= dstFilter->length - 1;
	filter2= (double*)memalign(8, filter2Size*dstW*sizeof(double));

	for(i=0; i<dstW; i++)
	{
		int j;
		SwsVector scaleFilter;
		SwsVector *outVec;

		scaleFilter.coeff= filter + i*filterSize;
		scaleFilter.length= filterSize;

		if(srcFilter) outVec= getConvVec(srcFilter, &scaleFilter);
		else	      outVec= &scaleFilter;

		ASSERT(outVec->length == filter2Size)
		//FIXME dstFilter

		for(j=0; j<outVec->length; j++)
		{
			filter2[i*filter2Size + j]= outVec->coeff[j];
		}

		(*filterPos)[i]+= (filterSize-1)/2 - (filter2Size-1)/2;

		if(outVec != &scaleFilter) freeVec(outVec);
	}
	free(filter); filter=NULL;

	/* try to reduce the filter-size (step1 find size and shift left) */
	// Assume its near normalized (*0.5 or *2.0 is ok but * 0.001 is not)
	minFilterSize= 0;
	for(i=dstW-1; i>=0; i--)
	{
		int min= filter2Size;
		int j;
		double cutOff=0.0;

		/* get rid off near zero elements on the left by shifting left */
		for(j=0; j<filter2Size; j++)
		{
			int k;
			cutOff += ABS(filter2[i*filter2Size]);

			if(cutOff > SWS_MAX_REDUCE_CUTOFF) break;

			/* preserve Monotonicity because the core cant handle the filter otherwise */
			if(i<dstW-1 && (*filterPos)[i] >= (*filterPos)[i+1]) break;

			// Move filter coeffs left
			for(k=1; k<filter2Size; k++)
				filter2[i*filter2Size + k - 1]= filter2[i*filter2Size + k];
			filter2[i*filter2Size + k - 1]= 0.0;
			(*filterPos)[i]++;
		}

		cutOff=0.0;
		/* count near zeros on the right */
		for(j=filter2Size-1; j>0; j--)
		{
			cutOff += ABS(filter2[i*filter2Size + j]);

			if(cutOff > SWS_MAX_REDUCE_CUTOFF) break;
			min--;
		}

		if(min>minFilterSize) minFilterSize= min;
	}

	/* try to reduce the filter-size (step2 reduce it) */
	for(i=0; i<dstW; i++)
	{
		int j;

		for(j=0; j<minFilterSize; j++)
			filter2[i*minFilterSize + j]= filter2[i*filter2Size + j];
	}
	if((flags&SWS_PRINT_INFO) && verbose)
		printf("SwScaler: reducing filtersize %d -> %d\n", filter2Size, minFilterSize);
	filter2Size= minFilterSize;
	ASSERT(filter2Size > 0)

	//FIXME try to align filterpos if possible

	//fix borders
	for(i=0; i<dstW; i++)
	{
		int j;
		if((*filterPos)[i] < 0)
		{
			// Move filter coeffs left to compensate for filterPos
			for(j=1; j<filter2Size; j++)
			{
				int left= MAX(j + (*filterPos)[i], 0);
				filter2[i*filter2Size + left] += filter2[i*filter2Size + j];
				filter2[i*filter2Size + j]=0;
			}
			(*filterPos)[i]= 0;
		}

		if((*filterPos)[i] + filter2Size > srcW)
		{
			int shift= (*filterPos)[i] + filter2Size - srcW;
			// Move filter coeffs right to compensate for filterPos
			for(j=filter2Size-2; j>=0; j--)
			{
				int right= MIN(j + shift, filter2Size-1);
				filter2[i*filter2Size +right] += filter2[i*filter2Size +j];
				filter2[i*filter2Size +j]=0;
			}
			(*filterPos)[i]= srcW - filter2Size;
		}
	}


	*outFilterSize= (filter2Size +(filterAlign-1)) & (~(filterAlign-1));
	*outFilter= (int16_t*)memalign(8, *outFilterSize*dstW*sizeof(int16_t));
	memset(*outFilter, 0, *outFilterSize*dstW*sizeof(int16_t));

	/* Normalize & Store in outFilter */
	for(i=0; i<dstW; i++)
	{
		int j;
		double sum=0;
		double scale= one;
		for(j=0; j<filter2Size; j++)
		{
			sum+= filter2[i*filter2Size + j];
		}
		scale/= sum;
		for(j=0; j<filter2Size; j++)
		{
			(*outFilter)[i*(*outFilterSize) + j]= (int)(filter2[i*filter2Size + j]*scale);
		}
	}

	free(filter2);
}

#ifdef ARCH_X86
static void initMMX2HScaler(int dstW, int xInc, uint8_t *funnyCode)
{
	uint8_t *fragment;
	int imm8OfPShufW1;
	int imm8OfPShufW2;
	int fragmentLength;

	int xpos, i;

	// create an optimized horizontal scaling routine

	//code fragment

	asm volatile(
		"jmp 9f				\n\t"
	// Begin
		"0:				\n\t"
		"movq (%%esi), %%mm0		\n\t" //FIXME Alignment
		"movq %%mm0, %%mm1		\n\t"
		"psrlq $8, %%mm0		\n\t"
		"punpcklbw %%mm7, %%mm1	\n\t"
		"movq %%mm2, %%mm3		\n\t"
		"punpcklbw %%mm7, %%mm0	\n\t"
		"addw %%bx, %%cx		\n\t" //2*xalpha += (4*lumXInc)&0xFFFF
		"pshufw $0xFF, %%mm1, %%mm1	\n\t"
		"1:				\n\t"
		"adcl %%edx, %%esi		\n\t" //xx+= (4*lumXInc)>>16 + carry
		"pshufw $0xFF, %%mm0, %%mm0	\n\t"
		"2:				\n\t"
		"psrlw $9, %%mm3		\n\t"
		"psubw %%mm1, %%mm0		\n\t"
		"pmullw %%mm3, %%mm0		\n\t"
		"paddw %%mm6, %%mm2		\n\t" // 2*alpha += xpos&0xFFFF
		"psllw $7, %%mm1		\n\t"
		"paddw %%mm1, %%mm0		\n\t"

		"movq %%mm0, (%%edi, %%eax)	\n\t"

		"addl $8, %%eax			\n\t"
	// End
		"9:				\n\t"
//		"int $3\n\t"
		"leal 0b, %0			\n\t"
		"leal 1b, %1			\n\t"
		"leal 2b, %2			\n\t"
		"decl %1			\n\t"
		"decl %2			\n\t"
		"subl %0, %1			\n\t"
		"subl %0, %2			\n\t"
		"leal 9b, %3			\n\t"
		"subl %0, %3			\n\t"
		:"=r" (fragment), "=r" (imm8OfPShufW1), "=r" (imm8OfPShufW2),
		"=r" (fragmentLength)
	);

	xpos= 0; //lumXInc/2 - 0x8000; // difference between pixel centers

	for(i=0; i<dstW/8; i++)
	{
		int xx=xpos>>16;

		if((i&3) == 0)
		{
			int a=0;
			int b=((xpos+xInc)>>16) - xx;
			int c=((xpos+xInc*2)>>16) - xx;
			int d=((xpos+xInc*3)>>16) - xx;

			memcpy(funnyCode + fragmentLength*i/4, fragment, fragmentLength);

			funnyCode[fragmentLength*i/4 + imm8OfPShufW1]=
			funnyCode[fragmentLength*i/4 + imm8OfPShufW2]=
				a | (b<<2) | (c<<4) | (d<<6);

			// if we dont need to read 8 bytes than dont :), reduces the chance of
			// crossing a cache line
			if(d<3) funnyCode[fragmentLength*i/4 + 1]= 0x6E;

			funnyCode[fragmentLength*(i+4)/4]= RET;
		}
		xpos+=xInc;
	}
}
#endif // ARCH_X86

//FIXME remove
void SwScale_Init(){
}

static void globalInit(){
    // generating tables:
    int i;
    for(i=0; i<768; i++){
	int c= MIN(MAX(i-256, 0), 255);
	clip_table[i]=c;
	yuvtab_2568[c]= clip_yuvtab_2568[i]=(0x2568*(c-16))+(256<<13);
	yuvtab_3343[c]= clip_yuvtab_3343[i]=0x3343*(c-128);
	yuvtab_0c92[c]= clip_yuvtab_0c92[i]=-0x0c92*(c-128);
	yuvtab_1a1e[c]= clip_yuvtab_1a1e[i]=-0x1a1e*(c-128);
	yuvtab_40cf[c]= clip_yuvtab_40cf[i]=0x40cf*(c-128);
    }

    for(i=0; i<768; i++)
    {
	int v= clip_table[i];
	clip_table16b[i]= v>>3;
	clip_table16g[i]= (v<<3)&0x07E0;
	clip_table16r[i]= (v<<8)&0xF800;
	clip_table15b[i]= v>>3;
	clip_table15g[i]= (v<<2)&0x03E0;
	clip_table15r[i]= (v<<7)&0x7C00;
    }

cpuCaps= gCpuCaps;

#ifdef RUNTIME_CPUDETECT
#ifdef CAN_COMPILE_X86_ASM
	// ordered per speed fasterst first
	if(gCpuCaps.hasMMX2)
		swScale= swScale_MMX2;
	else if(gCpuCaps.has3DNow)
		swScale= swScale_3DNow;
	else if(gCpuCaps.hasMMX)
		swScale= swScale_MMX;
	else
		swScale= swScale_C;

#else
	swScale= swScale_C;
	cpuCaps.hasMMX2 = cpuCaps.hasMMX = cpuCaps.has3DNow = 0;
#endif
#else //RUNTIME_CPUDETECT
#ifdef HAVE_MMX2
	swScale= swScale_MMX2;
	cpuCaps.has3DNow = 0;
#elif defined (HAVE_3DNOW)
	swScale= swScale_3DNow;
	cpuCaps.hasMMX2 = 0;
#elif defined (HAVE_MMX)
	swScale= swScale_MMX;
	cpuCaps.hasMMX2 = cpuCaps.has3DNow = 0;
#else
	swScale= swScale_C;
	cpuCaps.hasMMX2 = cpuCaps.hasMMX = cpuCaps.has3DNow = 0;
#endif
#endif //!RUNTIME_CPUDETECT
}


SwsContext *getSwsContext(int srcW, int srcH, int srcFormat, int dstW, int dstH, int dstFormat, int flags,
                         SwsFilter *srcFilter, SwsFilter *dstFilter){

	const int widthAlign= dstFormat==IMGFMT_YV12 ? 16 : 8;
	SwsContext *c;
	int i;
	SwsFilter dummyFilter= {NULL, NULL, NULL, NULL};

#ifdef ARCH_X86
	if(gCpuCaps.hasMMX)
		asm volatile("emms\n\t"::: "memory");
#endif

	if(swScale==NULL) globalInit();

	/* sanity check */
	if(srcW<1 || srcH<1 || dstW<1 || dstH<1) return NULL;

/* FIXME
	if(dstStride[0]%widthAlign !=0 )
	{
		if(flags & SWS_PRINT_INFO)
			fprintf(stderr, "SwScaler: Warning: dstStride is not a multiple of %d!\n"
					"SwScaler:          ->cannot do aligned memory acesses anymore\n",
					widthAlign);
	}
*/
	if(!dstFilter) dstFilter= &dummyFilter;
	if(!srcFilter) srcFilter= &dummyFilter;

	c= memalign(64, sizeof(SwsContext));
	memset(c, 0, sizeof(SwsContext));

	c->srcW= srcW;
	c->srcH= srcH;
	c->dstW= dstW;
	c->dstH= dstH;
	c->lumXInc= ((srcW<<16) + (dstW>>1))/dstW;
	c->lumYInc= ((srcH<<16) + (dstH>>1))/dstH;
	c->flags= flags;
	c->dstFormat= dstFormat;
	c->srcFormat= srcFormat;

	if(cpuCaps.hasMMX2)
	{
		c->canMMX2BeUsed= (dstW >=srcW && (dstW&31)==0 && (srcW&15)==0) ? 1 : 0;
		if(!c->canMMX2BeUsed && dstW >=srcW && (srcW&15)==0 && (flags&SWS_FAST_BILINEAR))
		{
			if(flags&SWS_PRINT_INFO)
				fprintf(stderr, "SwScaler: output Width is not a multiple of 32 -> no MMX2 scaler\n");
		}
	}
	else
		c->canMMX2BeUsed=0;

	// match pixel 0 of the src to pixel 0 of dst and match pixel n-2 of src to pixel n-2 of dst
	// but only for the FAST_BILINEAR mode otherwise do correct scaling
	// n-2 is the last chrominance sample available
	// this is not perfect, but noone shuld notice the difference, the more correct variant
	// would be like the vertical one, but that would require some special code for the
	// first and last pixel
	if(flags&SWS_FAST_BILINEAR)
	{
		if(c->canMMX2BeUsed) 	c->lumXInc+= 20;
		//we dont use the x86asm scaler if mmx is available
		else if(cpuCaps.hasMMX)	c->lumXInc = ((srcW-2)<<16)/(dstW-2) - 20;
	}

	/* set chrXInc & chrDstW */
	if((flags&SWS_FULL_UV_IPOL) && dstFormat!=IMGFMT_YV12)
		c->chrXInc= c->lumXInc>>1, c->chrDstW= dstW;
	else
		c->chrXInc= c->lumXInc,    c->chrDstW= (dstW+1)>>1;

	/* set chrYInc & chrDstH */
	if(dstFormat==IMGFMT_YV12)	c->chrYInc= c->lumYInc,    c->chrDstH= (dstH+1)>>1;
	else				c->chrYInc= c->lumYInc>>1, c->chrDstH= dstH;

	/* precalculate horizontal scaler filter coefficients */
	{
		const int filterAlign= cpuCaps.hasMMX ? 4 : 1;

		initFilter(&c->hLumFilter, &c->hLumFilterPos, &c->hLumFilterSize, c->lumXInc,
				 srcW      ,       dstW, filterAlign, 1<<14, flags,
				 srcFilter->lumH, dstFilter->lumH);
		initFilter(&c->hChrFilter, &c->hChrFilterPos, &c->hChrFilterSize, c->chrXInc,
				(srcW+1)>>1, c->chrDstW, filterAlign, 1<<14, flags,
				 srcFilter->chrH, dstFilter->chrH);

#ifdef ARCH_X86
// cant downscale !!!
		if(c->canMMX2BeUsed && (flags & SWS_FAST_BILINEAR))
		{
			initMMX2HScaler(      dstW, c->lumXInc, c->funnyYCode);
			initMMX2HScaler(c->chrDstW, c->chrXInc, c->funnyUVCode);
		}
#endif
	} // Init Horizontal stuff



	/* precalculate vertical scaler filter coefficients */
	initFilter(&c->vLumFilter, &c->vLumFilterPos, &c->vLumFilterSize, c->lumYInc,
			srcH      ,        dstH, 1, (1<<12)-4, flags,
			srcFilter->lumV, dstFilter->lumV);
	initFilter(&c->vChrFilter, &c->vChrFilterPos, &c->vChrFilterSize, c->chrYInc,
			(srcH+1)>>1, c->chrDstH, 1, (1<<12)-4, flags,
			 srcFilter->chrV, dstFilter->chrV);

	// Calculate Buffer Sizes so that they wont run out while handling these damn slices
	c->vLumBufSize= c->vLumFilterSize;
	c->vChrBufSize= c->vChrFilterSize;
	for(i=0; i<dstH; i++)
	{
		int chrI= i*c->chrDstH / dstH;
		int nextSlice= MAX(c->vLumFilterPos[i   ] + c->vLumFilterSize - 1,
				 ((c->vChrFilterPos[chrI] + c->vChrFilterSize - 1)<<1));
		nextSlice&= ~1; // Slices start at even boundaries
		if(c->vLumFilterPos[i   ] + c->vLumBufSize < nextSlice)
			c->vLumBufSize= nextSlice - c->vLumFilterPos[i   ];
		if(c->vChrFilterPos[chrI] + c->vChrBufSize < (nextSlice>>1))
			c->vChrBufSize= (nextSlice>>1) - c->vChrFilterPos[chrI];
	}

	// allocate pixbufs (we use dynamic allocation because otherwise we would need to
	// allocate several megabytes to handle all possible cases)
	c->lumPixBuf= (int16_t**)memalign(4, c->vLumBufSize*2*sizeof(int16_t*));
	c->chrPixBuf= (int16_t**)memalign(4, c->vChrBufSize*2*sizeof(int16_t*));
	for(i=0; i<c->vLumBufSize; i++)
		c->lumPixBuf[i]= c->lumPixBuf[i+c->vLumBufSize]= (uint16_t*)memalign(8, 4000);
	for(i=0; i<c->vChrBufSize; i++)
		c->chrPixBuf[i]= c->chrPixBuf[i+c->vChrBufSize]= (uint16_t*)memalign(8, 8000);

	//try to avoid drawing green stuff between the right end and the stride end
	for(i=0; i<c->vLumBufSize; i++) memset(c->lumPixBuf[i], 0, 4000);
	for(i=0; i<c->vChrBufSize; i++) memset(c->chrPixBuf[i], 64, 8000);

	ASSERT(c->chrDstH <= dstH)

	// pack filter data for mmx code
	if(cpuCaps.hasMMX)
	{
		c->lumMmxFilter= (int16_t*)memalign(8, c->vLumFilterSize*      dstH*4*sizeof(int16_t));
		c->chrMmxFilter= (int16_t*)memalign(8, c->vChrFilterSize*c->chrDstH*4*sizeof(int16_t));
		for(i=0; i<c->vLumFilterSize*dstH; i++)
			c->lumMmxFilter[4*i]=c->lumMmxFilter[4*i+1]=c->lumMmxFilter[4*i+2]=c->lumMmxFilter[4*i+3]=
				c->vLumFilter[i];
		for(i=0; i<c->vChrFilterSize*c->chrDstH; i++)
			c->chrMmxFilter[4*i]=c->chrMmxFilter[4*i+1]=c->chrMmxFilter[4*i+2]=c->chrMmxFilter[4*i+3]=
				c->vChrFilter[i];
	}

	if(flags&SWS_PRINT_INFO)
	{
#ifdef DITHER1XBPP
		char *dither= " dithered";
#else
		char *dither= "";
#endif
		if(flags&SWS_FAST_BILINEAR)
			fprintf(stderr, "\nSwScaler: FAST_BILINEAR scaler ");
		else if(flags&SWS_BILINEAR)
			fprintf(stderr, "\nSwScaler: BILINEAR scaler ");
		else if(flags&SWS_BICUBIC)
			fprintf(stderr, "\nSwScaler: BICUBIC scaler ");
		else if(flags&SWS_POINT)
			fprintf(stderr, "\nSwScaler: POINT scaler ");
		else
			fprintf(stderr, "\nSwScaler: ehh flags invalid?! ");

		if(dstFormat==IMGFMT_BGR15)
			fprintf(stderr, "with%s BGR15 output ", dither);
		else if(dstFormat==IMGFMT_BGR16)
			fprintf(stderr, "with%s BGR16 output ", dither);
		else if(dstFormat==IMGFMT_BGR24)
			fprintf(stderr, "with BGR24 output ");
		else if(dstFormat==IMGFMT_BGR32)
			fprintf(stderr, "with BGR32 output ");
		else if(dstFormat==IMGFMT_YV12)
			fprintf(stderr, "with YV12 output ");
		else
			fprintf(stderr, "without output ");

		if(cpuCaps.hasMMX2)
			fprintf(stderr, "using MMX2\n");
		else if(cpuCaps.has3DNow)
			fprintf(stderr, "using 3DNOW\n");
		else if(cpuCaps.hasMMX)
			fprintf(stderr, "using MMX\n");
		else
			fprintf(stderr, "using C\n");
	}

	if((flags & SWS_PRINT_INFO) && verbose)
	{
		if(cpuCaps.hasMMX)
		{
			if(c->canMMX2BeUsed && (flags&SWS_FAST_BILINEAR))
				printf("SwScaler: using FAST_BILINEAR MMX2 scaler for horizontal scaling\n");
			else
			{
				if(c->hLumFilterSize==4)
					printf("SwScaler: using 4-tap MMX scaler for horizontal luminance scaling\n");
				else if(c->hLumFilterSize==8)
					printf("SwScaler: using 8-tap MMX scaler for horizontal luminance scaling\n");
				else
					printf("SwScaler: using n-tap MMX scaler for horizontal luminance scaling\n");

				if(c->hChrFilterSize==4)
					printf("SwScaler: using 4-tap MMX scaler for horizontal chrominance scaling\n");
				else if(c->hChrFilterSize==8)
					printf("SwScaler: using 8-tap MMX scaler for horizontal chrominance scaling\n");
				else
					printf("SwScaler: using n-tap MMX scaler for horizontal chrominance scaling\n");
			}
		}
		else
		{
#ifdef ARCH_X86
			printf("SwScaler: using X86-Asm scaler for horizontal scaling\n");
#else
			if(flags & SWS_FAST_BILINEAR)
				printf("SwScaler: using FAST_BILINEAR C scaler for horizontal scaling\n");
			else
				printf("SwScaler: using C scaler for horizontal scaling\n");
#endif
		}
		if(dstFormat==IMGFMT_YV12)
		{
			if(c->vLumFilterSize==1)
				printf("SwScaler: using 1-tap %s \"scaler\" for vertical scaling (YV12)\n", cpuCaps.hasMMX ? "MMX" : "C");
			else
				printf("SwScaler: using n-tap %s scaler for vertical scaling (YV12)\n", cpuCaps.hasMMX ? "MMX" : "C");
		}
		else
		{
			if(c->vLumFilterSize==1 && c->vChrFilterSize==2)
				printf("SwScaler: using 1-tap %s \"scaler\" for vertical luminance scaling (BGR)\n"
				       "SwScaler:       2-tap scaler for vertical chrominance scaling (BGR)\n",cpuCaps.hasMMX ? "MMX" : "C");
			else if(c->vLumFilterSize==2 && c->vChrFilterSize==2)
				printf("SwScaler: using 2-tap linear %s scaler for vertical scaling (BGR)\n", cpuCaps.hasMMX ? "MMX" : "C");
			else
				printf("SwScaler: using n-tap %s scaler for vertical scaling (BGR)\n", cpuCaps.hasMMX ? "MMX" : "C");
		}

		if(dstFormat==IMGFMT_BGR24)
			printf("SwScaler: using %s YV12->BGR24 Converter\n",
				cpuCaps.hasMMX2 ? "MMX2" : (cpuCaps.hasMMX ? "MMX" : "C"));
		else if(dstFormat==IMGFMT_BGR32)
			printf("SwScaler: using %s YV12->BGR32 Converter\n", cpuCaps.hasMMX ? "MMX" : "C");
		else if(dstFormat==IMGFMT_BGR16)
			printf("SwScaler: using %s YV12->BGR16 Converter\n", cpuCaps.hasMMX ? "MMX" : "C");
		else if(dstFormat==IMGFMT_BGR15)
			printf("SwScaler: using %s YV12->BGR15 Converter\n", cpuCaps.hasMMX ? "MMX" : "C");

		printf("SwScaler: %dx%d -> %dx%d\n", srcW, srcH, dstW, dstH);
	}

	return c;
}

/**
 * returns a normalized gaussian curve used to filter stuff
 * quality=3 is high quality, lowwer is lowwer quality
 */

SwsVector *getGaussianVec(double variance, double quality){
	const int length= (int)(variance*quality + 0.5) | 1;
	int i;
	double *coeff= memalign(sizeof(double), length*sizeof(double));
	double middle= (length-1)*0.5;
	SwsVector *vec= malloc(sizeof(SwsVector));

	vec->coeff= coeff;
	vec->length= length;

	for(i=0; i<length; i++)
	{
		double dist= i-middle;
		coeff[i]= exp( -dist*dist/(2*variance*variance) ) / sqrt(2*variance*PI);
	}

	normalizeVec(vec, 1.0);

	return vec;
}

SwsVector *getConstVec(double c, int length){
	int i;
	double *coeff= memalign(sizeof(double), length*sizeof(double));
	SwsVector *vec= malloc(sizeof(SwsVector));

	vec->coeff= coeff;
	vec->length= length;

	for(i=0; i<length; i++)
		coeff[i]= c;

	return vec;
}


SwsVector *getIdentityVec(void){
	double *coeff= memalign(sizeof(double), sizeof(double));
	SwsVector *vec= malloc(sizeof(SwsVector));
	coeff[0]= 1.0;

	vec->coeff= coeff;
	vec->length= 1;

	return vec;
}

void normalizeVec(SwsVector *a, double height){
	int i;
	double sum=0;
	double inv;

	for(i=0; i<a->length; i++)
		sum+= a->coeff[i];

	inv= height/sum;

	for(i=0; i<a->length; i++)
		a->coeff[i]*= height;
}

void scaleVec(SwsVector *a, double scalar){
	int i;

	for(i=0; i<a->length; i++)
		a->coeff[i]*= scalar;
}

static SwsVector *getConvVec(SwsVector *a, SwsVector *b){
	int length= a->length + b->length - 1;
	double *coeff= memalign(sizeof(double), length*sizeof(double));
	int i, j;
	SwsVector *vec= malloc(sizeof(SwsVector));

	vec->coeff= coeff;
	vec->length= length;

	for(i=0; i<length; i++) coeff[i]= 0.0;

	for(i=0; i<a->length; i++)
	{
		for(j=0; j<b->length; j++)
		{
			coeff[i+j]+= a->coeff[i]*b->coeff[j];
		}
	}

	return vec;
}

static SwsVector *sumVec(SwsVector *a, SwsVector *b){
	int length= MAX(a->length, b->length);
	double *coeff= memalign(sizeof(double), length*sizeof(double));
	int i;
	SwsVector *vec= malloc(sizeof(SwsVector));

	vec->coeff= coeff;
	vec->length= length;

	for(i=0; i<length; i++) coeff[i]= 0.0;

	for(i=0; i<a->length; i++) coeff[i + (length-1)/2 - (a->length-1)/2]+= a->coeff[i];
	for(i=0; i<b->length; i++) coeff[i + (length-1)/2 - (b->length-1)/2]+= b->coeff[i];

	return vec;
}

static SwsVector *diffVec(SwsVector *a, SwsVector *b){
	int length= MAX(a->length, b->length);
	double *coeff= memalign(sizeof(double), length*sizeof(double));
	int i;
	SwsVector *vec= malloc(sizeof(SwsVector));

	vec->coeff= coeff;
	vec->length= length;

	for(i=0; i<length; i++) coeff[i]= 0.0;

	for(i=0; i<a->length; i++) coeff[i + (length-1)/2 - (a->length-1)/2]+= a->coeff[i];
	for(i=0; i<b->length; i++) coeff[i + (length-1)/2 - (b->length-1)/2]-= b->coeff[i];

	return vec;
}

/* shift left / or right if "shift" is negative */
static SwsVector *getShiftedVec(SwsVector *a, int shift){
	int length= a->length + ABS(shift)*2;
	double *coeff= memalign(sizeof(double), length*sizeof(double));
	int i;
	SwsVector *vec= malloc(sizeof(SwsVector));

	vec->coeff= coeff;
	vec->length= length;

	for(i=0; i<length; i++) coeff[i]= 0.0;

	for(i=0; i<a->length; i++)
	{
		coeff[i + (length-1)/2 - (a->length-1)/2 - shift]= a->coeff[i];
	}

	return vec;
}

void shiftVec(SwsVector *a, int shift){
	SwsVector *shifted= getShiftedVec(a, shift);
	free(a->coeff);
	a->coeff= shifted->coeff;
	a->length= shifted->length;
	free(shifted);
}

void addVec(SwsVector *a, SwsVector *b){
	SwsVector *sum= sumVec(a, b);
	free(a->coeff);
	a->coeff= sum->coeff;
	a->length= sum->length;
	free(sum);
}

void subVec(SwsVector *a, SwsVector *b){
	SwsVector *diff= diffVec(a, b);
	free(a->coeff);
	a->coeff= diff->coeff;
	a->length= diff->length;
	free(diff);
}

void convVec(SwsVector *a, SwsVector *b){
	SwsVector *conv= getConvVec(a, b);
	free(a->coeff);
	a->coeff= conv->coeff;
	a->length= conv->length;
	free(conv);
}

SwsVector *cloneVec(SwsVector *a){
	double *coeff= memalign(sizeof(double), a->length*sizeof(double));
	int i;
	SwsVector *vec= malloc(sizeof(SwsVector));

	vec->coeff= coeff;
	vec->length= a->length;

	for(i=0; i<a->length; i++) coeff[i]= a->coeff[i];

	return vec;
}

void printVec(SwsVector *a){
	int i;
	double max=0;
	double min=0;
	double range;

	for(i=0; i<a->length; i++)
		if(a->coeff[i]>max) max= a->coeff[i];

	for(i=0; i<a->length; i++)
		if(a->coeff[i]<min) min= a->coeff[i];

	range= max - min;

	for(i=0; i<a->length; i++)
	{
		int x= (int)((a->coeff[i]-min)*60.0/range +0.5);
		printf("%1.3f ", a->coeff[i]);
		for(;x>0; x--) printf(" ");
		printf("|\n");
	}
}

void freeVec(SwsVector *a){
	if(!a) return;
	if(a->coeff) free(a->coeff);
	a->coeff=NULL;
	a->length=0;
	free(a);
}

void freeSwsContext(SwsContext *c){
	int i;

	if(!c) return;

	if(c->lumPixBuf)
	{
		for(i=0; i<c->vLumBufSize*2; i++)
		{
			if(c->lumPixBuf[i]) free(c->lumPixBuf[i]);
			c->lumPixBuf[i]=NULL;
		}
		free(c->lumPixBuf);
		c->lumPixBuf=NULL;
	}

	if(c->chrPixBuf)
	{
		for(i=0; i<c->vChrBufSize*2; i++)
		{
			if(c->chrPixBuf[i]) free(c->chrPixBuf[i]);
			c->chrPixBuf[i]=NULL;
		}
		free(c->chrPixBuf);
		c->chrPixBuf=NULL;
	}

	if(c->vLumFilter) free(c->vLumFilter);
	c->vLumFilter = NULL;
	if(c->vChrFilter) free(c->vChrFilter);
	c->vChrFilter = NULL;
	if(c->hLumFilter) free(c->hLumFilter);
	c->hLumFilter = NULL;
	if(c->hChrFilter) free(c->hChrFilter);
	c->hChrFilter = NULL;

	if(c->vLumFilterPos) free(c->vLumFilterPos);
	c->vLumFilterPos = NULL;
	if(c->vChrFilterPos) free(c->vChrFilterPos);
	c->vChrFilterPos = NULL;
	if(c->hLumFilterPos) free(c->hLumFilterPos);
	c->hLumFilterPos = NULL;
	if(c->hChrFilterPos) free(c->hChrFilterPos);
	c->hChrFilterPos = NULL;

	if(c->lumMmxFilter) free(c->lumMmxFilter);
	c->lumMmxFilter = NULL;
	if(c->chrMmxFilter) free(c->chrMmxFilter);
	c->chrMmxFilter = NULL;

	free(c);
}


