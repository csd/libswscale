
// Software scaling and colorspace conversion routines for MPlayer

// Orginal C implementation by A'rpi/ESP-team <arpi@thot.banki.hu>
// current version mostly by Michael Niedermayer (michaelni@gmx.at)
// the parts written by michael are under GNU GPL

#include <inttypes.h>
#include <string.h>
#include "../config.h"
#include "swscale.h"

//#undef HAVE_MMX2
//#undef HAVE_MMX
//#undef ARCH_X86
#define DITHER1XBPP
int fullUVIpol=0;
//disables the unscaled height version
int allwaysIpol=0;

#define RET 0xC3 //near return opcode
/*
NOTES

known BUGS with known cause (no bugreports please!, but patches are welcome :) )
horizontal MMX2 scaler reads 1-7 samples too much (might cause a sig11)

Supported output formats BGR15 BGR16 BGR24 BGR32
BGR15 & BGR16 MMX verions support dithering
Special versions: fast Y 1:1 scaling (no interpolation in y direction)

TODO
more intelligent missalignment avoidance for the horizontal scaler
*/

#define ABS(a) ((a) > 0 ? (a) : (-(a)))
#define MIN(a,b) ((a) > (b) ? (b) : (a))
#define MAX(a,b) ((a) < (b) ? (b) : (a))

#ifdef HAVE_MMX2
#define PAVGB(a,b) "pavgb " #a ", " #b " \n\t"
#elif defined (HAVE_3DNOW)
#define PAVGB(a,b) "pavgusb " #a ", " #b " \n\t"
#endif

#ifdef HAVE_MMX2
#define MOVNTQ(a,b) "movntq " #a ", " #b " \n\t"
#else
#define MOVNTQ(a,b) "movq " #a ", " #b " \n\t"
#endif


#ifdef HAVE_MMX
static uint64_t __attribute__((aligned(8))) yCoeff=    0x2568256825682568LL;
static uint64_t __attribute__((aligned(8))) vrCoeff=   0x3343334333433343LL;
static uint64_t __attribute__((aligned(8))) ubCoeff=   0x40cf40cf40cf40cfLL;
static uint64_t __attribute__((aligned(8))) vgCoeff=   0xE5E2E5E2E5E2E5E2LL;
static uint64_t __attribute__((aligned(8))) ugCoeff=   0xF36EF36EF36EF36ELL;
static uint64_t __attribute__((aligned(8))) w400=      0x0400040004000400LL;
static uint64_t __attribute__((aligned(8))) w80=       0x0080008000800080LL;
static uint64_t __attribute__((aligned(8))) w10=       0x0010001000100010LL;
static uint64_t __attribute__((aligned(8))) bm00001111=0x00000000FFFFFFFFLL;
static uint64_t __attribute__((aligned(8))) bm00000111=0x0000000000FFFFFFLL;
static uint64_t __attribute__((aligned(8))) bm11111000=0xFFFFFFFFFF000000LL;

static uint64_t __attribute__((aligned(8))) b16Dither= 0x0004000400040004LL;
static uint64_t __attribute__((aligned(8))) b16Dither1=0x0004000400040004LL;
static uint64_t __attribute__((aligned(8))) b16Dither2=0x0602060206020602LL;
static uint64_t __attribute__((aligned(8))) g16Dither= 0x0002000200020002LL;
static uint64_t __attribute__((aligned(8))) g16Dither1=0x0002000200020002LL;
static uint64_t __attribute__((aligned(8))) g16Dither2=0x0301030103010301LL;

static uint64_t __attribute__((aligned(8))) b16Mask=   0x001F001F001F001FLL;
static uint64_t __attribute__((aligned(8))) g16Mask=   0x07E007E007E007E0LL;
static uint64_t __attribute__((aligned(8))) r16Mask=   0xF800F800F800F800LL;
static uint64_t __attribute__((aligned(8))) b15Mask=   0x001F001F001F001FLL;
static uint64_t __attribute__((aligned(8))) g15Mask=   0x03E003E003E003E0LL;
static uint64_t __attribute__((aligned(8))) r15Mask=   0x7C007C007C007C00LL;

static uint64_t __attribute__((aligned(8))) temp0;
static uint64_t __attribute__((aligned(8))) asm_yalpha1;
static uint64_t __attribute__((aligned(8))) asm_uvalpha1;
#endif

// temporary storage for 4 yuv lines:
// 16bit for now (mmx likes it more compact)
#ifdef HAVE_MMX
static uint16_t __attribute__((aligned(8))) pix_buf_y[4][2048];
static uint16_t __attribute__((aligned(8))) pix_buf_uv[2][2048*2];
#else
static uint16_t pix_buf_y[4][2048];
static uint16_t pix_buf_uv[2][2048*2];
#endif

// clipping helper table for C implementations:
static unsigned char clip_table[768];

// yuv->rgb conversion tables:
static    int yuvtab_2568[256];
static    int yuvtab_3343[256];
static    int yuvtab_0c92[256];
static    int yuvtab_1a1e[256];
static    int yuvtab_40cf[256];


static uint8_t funnyYCode[10000];
static uint8_t funnyUVCode[10000];

static int canMMX2BeUsed=0;

#define FULL_YSCALEYUV2RGB \
		"pxor %%mm7, %%mm7		\n\t"\
		"movd %6, %%mm6			\n\t" /*yalpha1*/\
		"punpcklwd %%mm6, %%mm6		\n\t"\
		"punpcklwd %%mm6, %%mm6		\n\t"\
		"movd %7, %%mm5			\n\t" /*uvalpha1*/\
		"punpcklwd %%mm5, %%mm5		\n\t"\
		"punpcklwd %%mm5, %%mm5		\n\t"\
		"xorl %%eax, %%eax		\n\t"\
		"1:				\n\t"\
		"movq (%0, %%eax, 2), %%mm0	\n\t" /*buf0[eax]*/\
		"movq (%1, %%eax, 2), %%mm1	\n\t" /*buf1[eax]*/\
		"movq (%2, %%eax,2), %%mm2	\n\t" /* uvbuf0[eax]*/\
		"movq (%3, %%eax,2), %%mm3	\n\t" /* uvbuf1[eax]*/\
		"psubw %%mm1, %%mm0		\n\t" /* buf0[eax] - buf1[eax]*/\
		"psubw %%mm3, %%mm2		\n\t" /* uvbuf0[eax] - uvbuf1[eax]*/\
		"pmulhw %%mm6, %%mm0		\n\t" /* (buf0[eax] - buf1[eax])yalpha1>>16*/\
		"pmulhw %%mm5, %%mm2		\n\t" /* (uvbuf0[eax] - uvbuf1[eax])uvalpha1>>16*/\
		"psraw $4, %%mm1		\n\t" /* buf0[eax] - buf1[eax] >>4*/\
		"movq 4096(%2, %%eax,2), %%mm4	\n\t" /* uvbuf0[eax+2048]*/\
		"psraw $4, %%mm3		\n\t" /* uvbuf0[eax] - uvbuf1[eax] >>4*/\
		"paddw %%mm0, %%mm1		\n\t" /* buf0[eax]yalpha1 + buf1[eax](1-yalpha1) >>16*/\
		"movq 4096(%3, %%eax,2), %%mm0	\n\t" /* uvbuf1[eax+2048]*/\
		"paddw %%mm2, %%mm3		\n\t" /* uvbuf0[eax]uvalpha1 - uvbuf1[eax](1-uvalpha1)*/\
		"psubw %%mm0, %%mm4		\n\t" /* uvbuf0[eax+2048] - uvbuf1[eax+2048]*/\
		"psubw w80, %%mm1		\n\t" /* 8(Y-16)*/\
		"psubw w400, %%mm3		\n\t" /* 8(U-128)*/\
		"pmulhw yCoeff, %%mm1		\n\t"\
\
\
		"pmulhw %%mm5, %%mm4		\n\t" /* (uvbuf0[eax+2048] - uvbuf1[eax+2048])uvalpha1>>16*/\
		"movq %%mm3, %%mm2		\n\t" /* (U-128)8*/\
		"pmulhw ubCoeff, %%mm3		\n\t"\
		"psraw $4, %%mm0		\n\t" /* uvbuf0[eax+2048] - uvbuf1[eax+2048] >>4*/\
		"pmulhw ugCoeff, %%mm2		\n\t"\
		"paddw %%mm4, %%mm0		\n\t" /* uvbuf0[eax+2048]uvalpha1 - uvbuf1[eax+2048](1-uvalpha1)*/\
		"psubw w400, %%mm0		\n\t" /* (V-128)8*/\
\
\
		"movq %%mm0, %%mm4		\n\t" /* (V-128)8*/\
		"pmulhw vrCoeff, %%mm0		\n\t"\
		"pmulhw vgCoeff, %%mm4		\n\t"\
		"paddw %%mm1, %%mm3		\n\t" /* B*/\
		"paddw %%mm1, %%mm0		\n\t" /* R*/\
		"packuswb %%mm3, %%mm3		\n\t"\
\
		"packuswb %%mm0, %%mm0		\n\t"\
		"paddw %%mm4, %%mm2		\n\t"\
		"paddw %%mm2, %%mm1		\n\t" /* G*/\
\
		"packuswb %%mm1, %%mm1		\n\t"

#define YSCALEYUV2RGB \
		"movd %6, %%mm6			\n\t" /*yalpha1*/\
		"punpcklwd %%mm6, %%mm6		\n\t"\
		"punpcklwd %%mm6, %%mm6		\n\t"\
		"movq %%mm6, asm_yalpha1	\n\t"\
		"movd %7, %%mm5			\n\t" /*uvalpha1*/\
		"punpcklwd %%mm5, %%mm5		\n\t"\
		"punpcklwd %%mm5, %%mm5		\n\t"\
		"movq %%mm5, asm_uvalpha1	\n\t"\
		"xorl %%eax, %%eax		\n\t"\
		"1:				\n\t"\
		"movq (%2, %%eax), %%mm2	\n\t" /* uvbuf0[eax]*/\
		"movq (%3, %%eax), %%mm3	\n\t" /* uvbuf1[eax]*/\
		"movq 4096(%2, %%eax), %%mm5	\n\t" /* uvbuf0[eax+2048]*/\
		"movq 4096(%3, %%eax), %%mm4	\n\t" /* uvbuf1[eax+2048]*/\
		"psubw %%mm3, %%mm2		\n\t" /* uvbuf0[eax] - uvbuf1[eax]*/\
		"psubw %%mm4, %%mm5		\n\t" /* uvbuf0[eax+2048] - uvbuf1[eax+2048]*/\
		"movq asm_uvalpha1, %%mm0	\n\t"\
		"pmulhw %%mm0, %%mm2		\n\t" /* (uvbuf0[eax] - uvbuf1[eax])uvalpha1>>16*/\
		"pmulhw %%mm0, %%mm5		\n\t" /* (uvbuf0[eax+2048] - uvbuf1[eax+2048])uvalpha1>>16*/\
		"psraw $4, %%mm3		\n\t" /* uvbuf0[eax] - uvbuf1[eax] >>4*/\
		"psraw $4, %%mm4		\n\t" /* uvbuf0[eax+2048] - uvbuf1[eax+2048] >>4*/\
		"paddw %%mm2, %%mm3		\n\t" /* uvbuf0[eax]uvalpha1 - uvbuf1[eax](1-uvalpha1)*/\
		"paddw %%mm5, %%mm4		\n\t" /* uvbuf0[eax+2048]uvalpha1 - uvbuf1[eax+2048](1-uvalpha1)*/\
		"psubw w400, %%mm3		\n\t" /* (U-128)8*/\
		"psubw w400, %%mm4		\n\t" /* (V-128)8*/\
		"movq %%mm3, %%mm2		\n\t" /* (U-128)8*/\
		"movq %%mm4, %%mm5		\n\t" /* (V-128)8*/\
		"pmulhw ugCoeff, %%mm3		\n\t"\
		"pmulhw vgCoeff, %%mm4		\n\t"\
	/* mm2=(U-128)8, mm3=ug, mm4=vg mm5=(V-128)8 */\
		"movq (%0, %%eax, 2), %%mm0	\n\t" /*buf0[eax]*/\
		"movq (%1, %%eax, 2), %%mm1	\n\t" /*buf1[eax]*/\
		"movq 8(%0, %%eax, 2), %%mm6	\n\t" /*buf0[eax]*/\
		"movq 8(%1, %%eax, 2), %%mm7	\n\t" /*buf1[eax]*/\
		"psubw %%mm1, %%mm0		\n\t" /* buf0[eax] - buf1[eax]*/\
		"psubw %%mm7, %%mm6		\n\t" /* buf0[eax] - buf1[eax]*/\
		"pmulhw asm_yalpha1, %%mm0	\n\t" /* (buf0[eax] - buf1[eax])yalpha1>>16*/\
		"pmulhw asm_yalpha1, %%mm6	\n\t" /* (buf0[eax] - buf1[eax])yalpha1>>16*/\
		"psraw $4, %%mm1		\n\t" /* buf0[eax] - buf1[eax] >>4*/\
		"psraw $4, %%mm7		\n\t" /* buf0[eax] - buf1[eax] >>4*/\
		"paddw %%mm0, %%mm1		\n\t" /* buf0[eax]yalpha1 + buf1[eax](1-yalpha1) >>16*/\
		"paddw %%mm6, %%mm7		\n\t" /* buf0[eax]yalpha1 + buf1[eax](1-yalpha1) >>16*/\
		"pmulhw ubCoeff, %%mm2		\n\t"\
		"pmulhw vrCoeff, %%mm5		\n\t"\
		"psubw w80, %%mm1		\n\t" /* 8(Y-16)*/\
		"psubw w80, %%mm7		\n\t" /* 8(Y-16)*/\
		"pmulhw yCoeff, %%mm1		\n\t"\
		"pmulhw yCoeff, %%mm7		\n\t"\
	/* mm1= Y1, mm2=ub, mm3=ug, mm4=vg mm5=vr, mm7=Y2 */\
		"paddw %%mm3, %%mm4		\n\t"\
		"movq %%mm2, %%mm0		\n\t"\
		"movq %%mm5, %%mm6		\n\t"\
		"movq %%mm4, %%mm3		\n\t"\
		"punpcklwd %%mm2, %%mm2		\n\t"\
		"punpcklwd %%mm5, %%mm5		\n\t"\
		"punpcklwd %%mm4, %%mm4		\n\t"\
		"paddw %%mm1, %%mm2		\n\t"\
		"paddw %%mm1, %%mm5		\n\t"\
		"paddw %%mm1, %%mm4		\n\t"\
		"punpckhwd %%mm0, %%mm0		\n\t"\
		"punpckhwd %%mm6, %%mm6		\n\t"\
		"punpckhwd %%mm3, %%mm3		\n\t"\
		"paddw %%mm7, %%mm0		\n\t"\
		"paddw %%mm7, %%mm6		\n\t"\
		"paddw %%mm7, %%mm3		\n\t"\
		/* mm0=B1, mm2=B2, mm3=G2, mm4=G1, mm5=R1, mm6=R2 */\
		"packuswb %%mm0, %%mm2		\n\t"\
		"packuswb %%mm6, %%mm5		\n\t"\
		"packuswb %%mm3, %%mm4		\n\t"\
		"pxor %%mm7, %%mm7		\n\t"

#define YSCALEYUV2RGB1 \
		"xorl %%eax, %%eax		\n\t"\
		"1:				\n\t"\
		"movq (%2, %%eax), %%mm3	\n\t" /* uvbuf0[eax]*/\
		"movq 4096(%2, %%eax), %%mm4	\n\t" /* uvbuf0[eax+2048]*/\
		"psraw $4, %%mm3		\n\t" /* uvbuf0[eax] - uvbuf1[eax] >>4*/\
		"psraw $4, %%mm4		\n\t" /* uvbuf0[eax+2048] - uvbuf1[eax+2048] >>4*/\
		"psubw w400, %%mm3		\n\t" /* (U-128)8*/\
		"psubw w400, %%mm4		\n\t" /* (V-128)8*/\
		"movq %%mm3, %%mm2		\n\t" /* (U-128)8*/\
		"movq %%mm4, %%mm5		\n\t" /* (V-128)8*/\
		"pmulhw ugCoeff, %%mm3		\n\t"\
		"pmulhw vgCoeff, %%mm4		\n\t"\
	/* mm2=(U-128)8, mm3=ug, mm4=vg mm5=(V-128)8 */\
		"movq (%1, %%eax, 2), %%mm1	\n\t" /*buf0[eax]*/\
		"movq 8(%1, %%eax, 2), %%mm7	\n\t" /*buf0[eax]*/\
		"psraw $4, %%mm1		\n\t" /* buf0[eax] - buf1[eax] >>4*/\
		"psraw $4, %%mm7		\n\t" /* buf0[eax] - buf1[eax] >>4*/\
		"pmulhw ubCoeff, %%mm2		\n\t"\
		"pmulhw vrCoeff, %%mm5		\n\t"\
		"psubw w80, %%mm1		\n\t" /* 8(Y-16)*/\
		"psubw w80, %%mm7		\n\t" /* 8(Y-16)*/\
		"pmulhw yCoeff, %%mm1		\n\t"\
		"pmulhw yCoeff, %%mm7		\n\t"\
	/* mm1= Y1, mm2=ub, mm3=ug, mm4=vg mm5=vr, mm7=Y2 */\
		"paddw %%mm3, %%mm4		\n\t"\
		"movq %%mm2, %%mm0		\n\t"\
		"movq %%mm5, %%mm6		\n\t"\
		"movq %%mm4, %%mm3		\n\t"\
		"punpcklwd %%mm2, %%mm2		\n\t"\
		"punpcklwd %%mm5, %%mm5		\n\t"\
		"punpcklwd %%mm4, %%mm4		\n\t"\
		"paddw %%mm1, %%mm2		\n\t"\
		"paddw %%mm1, %%mm5		\n\t"\
		"paddw %%mm1, %%mm4		\n\t"\
		"punpckhwd %%mm0, %%mm0		\n\t"\
		"punpckhwd %%mm6, %%mm6		\n\t"\
		"punpckhwd %%mm3, %%mm3		\n\t"\
		"paddw %%mm7, %%mm0		\n\t"\
		"paddw %%mm7, %%mm6		\n\t"\
		"paddw %%mm7, %%mm3		\n\t"\
		/* mm0=B1, mm2=B2, mm3=G2, mm4=G1, mm5=R1, mm6=R2 */\
		"packuswb %%mm0, %%mm2		\n\t"\
		"packuswb %%mm6, %%mm5		\n\t"\
		"packuswb %%mm3, %%mm4		\n\t"\
		"pxor %%mm7, %%mm7		\n\t"

#define WRITEBGR32 \
		/* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */\
			"movq %%mm2, %%mm1		\n\t" /* B */\
			"movq %%mm5, %%mm6		\n\t" /* R */\
			"punpcklbw %%mm4, %%mm2		\n\t" /* GBGBGBGB 0 */\
			"punpcklbw %%mm7, %%mm5		\n\t" /* 0R0R0R0R 0 */\
			"punpckhbw %%mm4, %%mm1		\n\t" /* GBGBGBGB 2 */\
			"punpckhbw %%mm7, %%mm6		\n\t" /* 0R0R0R0R 2 */\
			"movq %%mm2, %%mm0		\n\t" /* GBGBGBGB 0 */\
			"movq %%mm1, %%mm3		\n\t" /* GBGBGBGB 2 */\
			"punpcklwd %%mm5, %%mm0		\n\t" /* 0RGB0RGB 0 */\
			"punpckhwd %%mm5, %%mm2		\n\t" /* 0RGB0RGB 1 */\
			"punpcklwd %%mm6, %%mm1		\n\t" /* 0RGB0RGB 2 */\
			"punpckhwd %%mm6, %%mm3		\n\t" /* 0RGB0RGB 3 */\
\
			MOVNTQ(%%mm0, (%4, %%eax, 4))\
			MOVNTQ(%%mm2, 8(%4, %%eax, 4))\
			MOVNTQ(%%mm1, 16(%4, %%eax, 4))\
			MOVNTQ(%%mm3, 24(%4, %%eax, 4))\
\
			"addl $8, %%eax			\n\t"\
			"cmpl %5, %%eax			\n\t"\
			" jb 1b				\n\t"

#define WRITEBGR16 \
			"movq %%mm2, %%mm1		\n\t" /* B */\
			"movq %%mm4, %%mm3		\n\t" /* G */\
			"movq %%mm5, %%mm6		\n\t" /* R */\
\
			"punpcklbw %%mm7, %%mm3		\n\t" /* 0G0G0G0G */\
			"punpcklbw %%mm7, %%mm2		\n\t" /* 0B0B0B0B */\
			"punpcklbw %%mm7, %%mm5		\n\t" /* 0R0R0R0R */\
\
			"psrlw $3, %%mm2		\n\t"\
			"psllw $3, %%mm3		\n\t"\
			"psllw $8, %%mm5		\n\t"\
\
			"pand g16Mask, %%mm3		\n\t"\
			"pand r16Mask, %%mm5		\n\t"\
\
			"por %%mm3, %%mm2		\n\t"\
			"por %%mm5, %%mm2		\n\t"\
\
			"punpckhbw %%mm7, %%mm4		\n\t" /* 0G0G0G0G */\
			"punpckhbw %%mm7, %%mm1		\n\t" /* 0B0B0B0B */\
			"punpckhbw %%mm7, %%mm6		\n\t" /* 0R0R0R0R */\
\
			"psrlw $3, %%mm1		\n\t"\
			"psllw $3, %%mm4		\n\t"\
			"psllw $8, %%mm6		\n\t"\
\
			"pand g16Mask, %%mm4		\n\t"\
			"pand r16Mask, %%mm6		\n\t"\
\
			"por %%mm4, %%mm1		\n\t"\
			"por %%mm6, %%mm1		\n\t"\
\
			MOVNTQ(%%mm2, (%4, %%eax, 2))\
			MOVNTQ(%%mm1, 8(%4, %%eax, 2))\
\
			"addl $8, %%eax			\n\t"\
			"cmpl %5, %%eax			\n\t"\
			" jb 1b				\n\t"

#define WRITEBGR15 \
			"movq %%mm2, %%mm1		\n\t" /* B */\
			"movq %%mm4, %%mm3		\n\t" /* G */\
			"movq %%mm5, %%mm6		\n\t" /* R */\
\
			"punpcklbw %%mm7, %%mm3		\n\t" /* 0G0G0G0G */\
			"punpcklbw %%mm7, %%mm2		\n\t" /* 0B0B0B0B */\
			"punpcklbw %%mm7, %%mm5		\n\t" /* 0R0R0R0R */\
\
			"psrlw $3, %%mm2		\n\t"\
			"psllw $2, %%mm3		\n\t"\
			"psllw $7, %%mm5		\n\t"\
\
			"pand g15Mask, %%mm3		\n\t"\
			"pand r15Mask, %%mm5		\n\t"\
\
			"por %%mm3, %%mm2		\n\t"\
			"por %%mm5, %%mm2		\n\t"\
\
			"punpckhbw %%mm7, %%mm4		\n\t" /* 0G0G0G0G */\
			"punpckhbw %%mm7, %%mm1		\n\t" /* 0B0B0B0B */\
			"punpckhbw %%mm7, %%mm6		\n\t" /* 0R0R0R0R */\
\
			"psrlw $3, %%mm1		\n\t"\
			"psllw $2, %%mm4		\n\t"\
			"psllw $7, %%mm6		\n\t"\
\
			"pand g15Mask, %%mm4		\n\t"\
			"pand r15Mask, %%mm6		\n\t"\
\
			"por %%mm4, %%mm1		\n\t"\
			"por %%mm6, %%mm1		\n\t"\
\
			MOVNTQ(%%mm2, (%4, %%eax, 2))\
			MOVNTQ(%%mm1, 8(%4, %%eax, 2))\
\
			"addl $8, %%eax			\n\t"\
			"cmpl %5, %%eax			\n\t"\
			" jb 1b				\n\t"
// FIXME find a faster way to shuffle it to BGR24
#define WRITEBGR24 \
		/* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */\
			"movq %%mm2, %%mm1		\n\t" /* B */\
			"movq %%mm5, %%mm6		\n\t" /* R */\
			"punpcklbw %%mm4, %%mm2		\n\t" /* GBGBGBGB 0 */\
			"punpcklbw %%mm7, %%mm5		\n\t" /* 0R0R0R0R 0 */\
			"punpckhbw %%mm4, %%mm1		\n\t" /* GBGBGBGB 2 */\
			"punpckhbw %%mm7, %%mm6		\n\t" /* 0R0R0R0R 2 */\
			"movq %%mm2, %%mm0		\n\t" /* GBGBGBGB 0 */\
			"movq %%mm1, %%mm3		\n\t" /* GBGBGBGB 2 */\
			"punpcklwd %%mm5, %%mm0		\n\t" /* 0RGB0RGB 0 */\
			"punpckhwd %%mm5, %%mm2		\n\t" /* 0RGB0RGB 1 */\
			"punpcklwd %%mm6, %%mm1		\n\t" /* 0RGB0RGB 2 */\
			"punpckhwd %%mm6, %%mm3		\n\t" /* 0RGB0RGB 3 */\
\
			"movq %%mm0, %%mm4		\n\t" /* 0RGB0RGB 0 */\
			"psrlq $8, %%mm0		\n\t" /* 00RGB0RG 0 */\
			"pand bm00000111, %%mm4		\n\t" /* 00000RGB 0 */\
			"pand bm11111000, %%mm0		\n\t" /* 00RGB000 0.5 */\
			"por %%mm4, %%mm0		\n\t" /* 00RGBRGB 0 */\
			"movq %%mm2, %%mm4		\n\t" /* 0RGB0RGB 1 */\
			"psllq $48, %%mm2		\n\t" /* GB000000 1 */\
			"por %%mm2, %%mm0		\n\t" /* GBRGBRGB 0 */\
\
			"movq %%mm4, %%mm2		\n\t" /* 0RGB0RGB 1 */\
			"psrld $16, %%mm4		\n\t" /* 000R000R 1 */\
			"psrlq $24, %%mm2		\n\t" /* 0000RGB0 1.5 */\
			"por %%mm4, %%mm2		\n\t" /* 000RRGBR 1 */\
			"pand bm00001111, %%mm2		\n\t" /* 0000RGBR 1 */\
			"movq %%mm1, %%mm4		\n\t" /* 0RGB0RGB 2 */\
			"psrlq $8, %%mm1		\n\t" /* 00RGB0RG 2 */\
			"pand bm00000111, %%mm4		\n\t" /* 00000RGB 2 */\
			"pand bm11111000, %%mm1		\n\t" /* 00RGB000 2.5 */\
			"por %%mm4, %%mm1		\n\t" /* 00RGBRGB 2 */\
			"movq %%mm1, %%mm4		\n\t" /* 00RGBRGB 2 */\
			"psllq $32, %%mm1		\n\t" /* BRGB0000 2 */\
			"por %%mm1, %%mm2		\n\t" /* BRGBRGBR 1 */\
\
			"psrlq $32, %%mm4		\n\t" /* 000000RG 2.5 */\
			"movq %%mm3, %%mm5		\n\t" /* 0RGB0RGB 3 */\
			"psrlq $8, %%mm3		\n\t" /* 00RGB0RG 3 */\
			"pand bm00000111, %%mm5		\n\t" /* 00000RGB 3 */\
			"pand bm11111000, %%mm3		\n\t" /* 00RGB000 3.5 */\
			"por %%mm5, %%mm3		\n\t" /* 00RGBRGB 3 */\
			"psllq $16, %%mm3		\n\t" /* RGBRGB00 3 */\
			"por %%mm4, %%mm3		\n\t" /* RGBRGBRG 2.5 */\
\
			"leal (%%eax, %%eax, 2), %%ebx	\n\t"\
			MOVNTQ(%%mm0, (%4, %%ebx))\
			MOVNTQ(%%mm2, 8(%4, %%ebx))\
			MOVNTQ(%%mm3, 16(%4, %%ebx))\
\
			"addl $8, %%eax			\n\t"\
			"cmpl %5, %%eax			\n\t"\
			" jb 1b				\n\t"


static inline void yuv2yuv(uint16_t *buf0, uint16_t *buf1, uint16_t *uvbuf0, uint16_t *uvbuf1,
			   uint8_t *dest, uint8_t *uDest, uint8_t *vDest, int dstw, int yalpha, int uvalpha)
{
	int yalpha1=yalpha^4095;
	int uvalpha1=uvalpha^4095;
	int i;

	for(i=0;i<dstw;i++)
	{
		((uint8_t*)dest)[0] = (buf0[i]*yalpha1+buf1[i]*yalpha)>>19;
		dest++;
	}

	if(uvalpha != -1)
	{
		for(i=0; i<dstw/2; i++)
		{
			((uint8_t*)uDest)[0] = (uvbuf0[i]*uvalpha1+uvbuf1[i]*uvalpha)>>19;
			((uint8_t*)vDest)[0] = (uvbuf0[i+2048]*uvalpha1+uvbuf1[i+2048]*uvalpha)>>19;
			uDest++;
			vDest++;
		}
	}
}

/**
 * vertical scale YV12 to RGB
 */
static inline void yuv2rgbX(uint16_t *buf0, uint16_t *buf1, uint16_t *uvbuf0, uint16_t *uvbuf1,
			    uint8_t *dest, int dstw, int yalpha, int uvalpha, int dstbpp)
{
	int yalpha1=yalpha^4095;
	int uvalpha1=uvalpha^4095;
	int i;

	if(fullUVIpol)
	{

#ifdef HAVE_MMX
		if(dstbpp == 32)
		{
			asm volatile(


FULL_YSCALEYUV2RGB
			"punpcklbw %%mm1, %%mm3		\n\t" // BGBGBGBG
			"punpcklbw %%mm7, %%mm0		\n\t" // R0R0R0R0

			"movq %%mm3, %%mm1		\n\t"
			"punpcklwd %%mm0, %%mm3		\n\t" // BGR0BGR0
			"punpckhwd %%mm0, %%mm1		\n\t" // BGR0BGR0

			MOVNTQ(%%mm3, (%4, %%eax, 4))
			MOVNTQ(%%mm1, 8(%4, %%eax, 4))

			"addl $4, %%eax			\n\t"
			"cmpl %5, %%eax			\n\t"
			" jb 1b				\n\t"


			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstw),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax"
			);
		}
		else if(dstbpp==24)
		{
			asm volatile(

FULL_YSCALEYUV2RGB

								// lsb ... msb
			"punpcklbw %%mm1, %%mm3		\n\t" // BGBGBGBG
			"punpcklbw %%mm7, %%mm0		\n\t" // R0R0R0R0

			"movq %%mm3, %%mm1		\n\t"
			"punpcklwd %%mm0, %%mm3		\n\t" // BGR0BGR0
			"punpckhwd %%mm0, %%mm1		\n\t" // BGR0BGR0

			"movq %%mm3, %%mm2		\n\t" // BGR0BGR0
			"psrlq $8, %%mm3		\n\t" // GR0BGR00
			"pand bm00000111, %%mm2		\n\t" // BGR00000
			"pand bm11111000, %%mm3		\n\t" // 000BGR00
			"por %%mm2, %%mm3		\n\t" // BGRBGR00
			"movq %%mm1, %%mm2		\n\t"
			"psllq $48, %%mm1		\n\t" // 000000BG
			"por %%mm1, %%mm3		\n\t" // BGRBGRBG

			"movq %%mm2, %%mm1		\n\t" // BGR0BGR0
			"psrld $16, %%mm2		\n\t" // R000R000
			"psrlq $24, %%mm1		\n\t" // 0BGR0000
			"por %%mm2, %%mm1		\n\t" // RBGRR000

			"movl %4, %%ebx			\n\t"
			"addl %%eax, %%ebx		\n\t"

#ifdef HAVE_MMX2
			//FIXME Alignment
			"movntq %%mm3, (%%ebx, %%eax, 2)\n\t"
			"movntq %%mm1, 8(%%ebx, %%eax, 2)\n\t"
#else
			"movd %%mm3, (%%ebx, %%eax, 2)	\n\t"
			"psrlq $32, %%mm3		\n\t"
			"movd %%mm3, 4(%%ebx, %%eax, 2)	\n\t"
			"movd %%mm1, 8(%%ebx, %%eax, 2)	\n\t"
#endif
			"addl $4, %%eax			\n\t"
			"cmpl %5, %%eax			\n\t"
			" jb 1b				\n\t"

			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "m" (dest), "m" (dstw),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax", "%ebx"
			);
		}
		else if(dstbpp==15)
		{
			asm volatile(

FULL_YSCALEYUV2RGB
#ifdef DITHER1XBPP
			"paddusb b16Dither, %%mm1	\n\t"
			"paddusb b16Dither, %%mm0	\n\t"
			"paddusb b16Dither, %%mm3	\n\t"
#endif
			"punpcklbw %%mm7, %%mm1		\n\t" // 0G0G0G0G
			"punpcklbw %%mm7, %%mm3		\n\t" // 0B0B0B0B
			"punpcklbw %%mm7, %%mm0		\n\t" // 0R0R0R0R

			"psrlw $3, %%mm3		\n\t"
			"psllw $2, %%mm1		\n\t"
			"psllw $7, %%mm0		\n\t"
			"pand g15Mask, %%mm1		\n\t"
			"pand r15Mask, %%mm0		\n\t"

			"por %%mm3, %%mm1		\n\t"
			"por %%mm1, %%mm0		\n\t"

			MOVNTQ(%%mm0, (%4, %%eax, 2))

			"addl $4, %%eax			\n\t"
			"cmpl %5, %%eax			\n\t"
			" jb 1b				\n\t"

			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstw),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax"
			);
		}
		else if(dstbpp==16)
		{
			asm volatile(

FULL_YSCALEYUV2RGB
#ifdef DITHER1XBPP
			"paddusb g16Dither, %%mm1	\n\t"
			"paddusb b16Dither, %%mm0	\n\t"
			"paddusb b16Dither, %%mm3	\n\t"
#endif
			"punpcklbw %%mm7, %%mm1		\n\t" // 0G0G0G0G
			"punpcklbw %%mm7, %%mm3		\n\t" // 0B0B0B0B
			"punpcklbw %%mm7, %%mm0		\n\t" // 0R0R0R0R

			"psrlw $3, %%mm3		\n\t"
			"psllw $3, %%mm1		\n\t"
			"psllw $8, %%mm0		\n\t"
			"pand g16Mask, %%mm1		\n\t"
			"pand r16Mask, %%mm0		\n\t"

			"por %%mm3, %%mm1		\n\t"
			"por %%mm1, %%mm0		\n\t"

			MOVNTQ(%%mm0, (%4, %%eax, 2))

			"addl $4, %%eax			\n\t"
			"cmpl %5, %%eax			\n\t"
			" jb 1b				\n\t"

			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstw),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax"
			);
		}
#else
		if(dstbpp==32 || dstbpp==24)
		{
			for(i=0;i<dstw;i++){
				// vertical linear interpolation && yuv2rgb in a single step:
				int Y=yuvtab_2568[((buf0[i]*yalpha1+buf1[i]*yalpha)>>19)];
				int U=((uvbuf0[i]*uvalpha1+uvbuf1[i]*uvalpha)>>19);
				int V=((uvbuf0[i+2048]*uvalpha1+uvbuf1[i+2048]*uvalpha)>>19);
				dest[0]=clip_table[((Y + yuvtab_40cf[U]) >>13)];
				dest[1]=clip_table[((Y + yuvtab_1a1e[V] + yuvtab_0c92[U]) >>13)];
				dest[2]=clip_table[((Y + yuvtab_3343[V]) >>13)];
				dest+=dstbpp>>3;
			}
		}
		else if(dstbpp==16)
		{
			for(i=0;i<dstw;i++){
				// vertical linear interpolation && yuv2rgb in a single step:
				int Y=yuvtab_2568[((buf0[i]*yalpha1+buf1[i]*yalpha)>>19)];
				int U=((uvbuf0[i]*uvalpha1+uvbuf1[i]*uvalpha)>>19);
				int V=((uvbuf0[i+2048]*uvalpha1+uvbuf1[i+2048]*uvalpha)>>19);

				((uint16_t*)dest)[0] =
					(clip_table[(Y + yuvtab_40cf[U]) >>13]>>3) |
					((clip_table[(Y + yuvtab_1a1e[V] + yuvtab_0c92[U]) >>13]<<3)&0x07E0) |
					((clip_table[(Y + yuvtab_3343[V]) >>13]<<8)&0xF800);
				dest+=2;
			}
		}
		else if(dstbpp==15)
		{
			for(i=0;i<dstw;i++){
				// vertical linear interpolation && yuv2rgb in a single step:
				int Y=yuvtab_2568[((buf0[i]*yalpha1+buf1[i]*yalpha)>>19)];
				int U=((uvbuf0[i]*uvalpha1+uvbuf1[i]*uvalpha)>>19);
				int V=((uvbuf0[i+2048]*uvalpha1+uvbuf1[i+2048]*uvalpha)>>19);

				((uint16_t*)dest)[0] =
					(clip_table[(Y + yuvtab_40cf[U]) >>13]>>3) |
					((clip_table[(Y + yuvtab_1a1e[V] + yuvtab_0c92[U]) >>13]<<2)&0x03E0) |
					((clip_table[(Y + yuvtab_3343[V]) >>13]<<7)&0x7C00);
				dest+=2;
			}
		}
#endif
	}//FULL_UV_IPOL
	else
	{
#ifdef HAVE_MMX
		if(dstbpp == 32)
		{
			asm volatile(
				YSCALEYUV2RGB
				WRITEBGR32

			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstw),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax"
			);
		}
		else if(dstbpp==24)
		{
			asm volatile(
				YSCALEYUV2RGB
				WRITEBGR24

			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstw),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax", "%ebx"
			);
		}
		else if(dstbpp==15)
		{
			asm volatile(
				YSCALEYUV2RGB
		/* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
				"paddusb b16Dither, %%mm2	\n\t"
				"paddusb b16Dither, %%mm4	\n\t"
				"paddusb b16Dither, %%mm5	\n\t"
#endif

				WRITEBGR15

			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstw),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax"
			);
		}
		else if(dstbpp==16)
		{
			asm volatile(
				YSCALEYUV2RGB
		/* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
				"paddusb g16Dither, %%mm2	\n\t"
				"paddusb b16Dither, %%mm4	\n\t"
				"paddusb b16Dither, %%mm5	\n\t"
#endif

				WRITEBGR16

			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstw),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax"
			);
		}
#else
//FIXME unroll C loop and dont recalculate UV
		if(dstbpp==32 || dstbpp==24)
		{
			for(i=0;i<dstw;i++){
				// vertical linear interpolation && yuv2rgb in a single step:
				int Y=yuvtab_2568[((buf0[i]*yalpha1+buf1[i]*yalpha)>>19)];
				int U=((uvbuf0[i/2]*uvalpha1+uvbuf1[i/2]*uvalpha)>>19);
				int V=((uvbuf0[i/2+2048]*uvalpha1+uvbuf1[i/2+2048]*uvalpha)>>19);
				dest[0]=clip_table[((Y + yuvtab_40cf[U]) >>13)];
				dest[1]=clip_table[((Y + yuvtab_1a1e[V] + yuvtab_0c92[U]) >>13)];
				dest[2]=clip_table[((Y + yuvtab_3343[V]) >>13)];
				dest+=dstbpp>>3;
			}
		}
		else if(dstbpp==16)
		{
			for(i=0;i<dstw;i++){
				// vertical linear interpolation && yuv2rgb in a single step:
				int Y=yuvtab_2568[((buf0[i]*yalpha1+buf1[i]*yalpha)>>19)];
				int U=((uvbuf0[i/2]*uvalpha1+uvbuf1[i/2]*uvalpha)>>19);
				int V=((uvbuf0[i/2+2048]*uvalpha1+uvbuf1[i/2+2048]*uvalpha)>>19);

				((uint16_t*)dest)[0] =
					(clip_table[(Y + yuvtab_40cf[U]) >>13]>>3) |
					((clip_table[(Y + yuvtab_1a1e[V] + yuvtab_0c92[U]) >>13]<<3)&0x07E0) |
					((clip_table[(Y + yuvtab_3343[V]) >>13]<<8)&0xF800);
				dest+=2;
			}
		}
		else if(dstbpp==15)
		{
			for(i=0;i<dstw;i++){
				// vertical linear interpolation && yuv2rgb in a single step:
				int Y=yuvtab_2568[((buf0[i]*yalpha1+buf1[i]*yalpha)>>19)];
				int U=((uvbuf0[i/2]*uvalpha1+uvbuf1[i/2]*uvalpha)>>19);
				int V=((uvbuf0[i/2+2048]*uvalpha1+uvbuf1[i/2+2048]*uvalpha)>>19);

				((uint16_t*)dest)[0] =
					(clip_table[(Y + yuvtab_40cf[U]) >>13]>>3) |
					((clip_table[(Y + yuvtab_1a1e[V] + yuvtab_0c92[U]) >>13]<<2)&0x03E0) |
					((clip_table[(Y + yuvtab_3343[V]) >>13]<<7)&0x7C00);
				dest+=2;
			}
		}
#endif
	} //!FULL_UV_IPOL
}

/**
 * YV12 to RGB without scaling or interpolating
 */
static inline void yuv2rgb1(uint16_t *buf0, uint16_t *buf1, uint16_t *uvbuf0, uint16_t *uvbuf1,
			    uint8_t *dest, int dstw, int yalpha, int uvalpha, int dstbpp)
{
	int yalpha1=yalpha^4095;
	int uvalpha1=uvalpha^4095;
	int i;
	if(fullUVIpol || allwaysIpol)
	{
		yuv2rgbX(buf0, buf1, uvbuf0, uvbuf1, dest, dstw, yalpha, uvalpha, dstbpp);
		return;
	}
#ifdef HAVE_MMX
		if(dstbpp == 32)
		{
			asm volatile(
				YSCALEYUV2RGB1
				WRITEBGR32
			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstw),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax"
			);
		}
		else if(dstbpp==24)
		{
			asm volatile(
				YSCALEYUV2RGB1
				WRITEBGR24
			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstw),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax", "%ebx"
			);
		}
		else if(dstbpp==15)
		{
			asm volatile(
				YSCALEYUV2RGB1
		/* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
				"paddusb b16Dither, %%mm2	\n\t"
				"paddusb b16Dither, %%mm4	\n\t"
				"paddusb b16Dither, %%mm5	\n\t"
#endif
				WRITEBGR15
			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstw),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax"
			);
		}
		else if(dstbpp==16)
		{
			asm volatile(
				YSCALEYUV2RGB1
		/* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
				"paddusb g16Dither, %%mm2	\n\t"
				"paddusb b16Dither, %%mm4	\n\t"
				"paddusb b16Dither, %%mm5	\n\t"
#endif

				WRITEBGR16
			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstw),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax"
			);
		}
#else
//FIXME unroll C loop and dont recalculate UV
		if(dstbpp==32 || dstbpp==24)
		{
			for(i=0;i<dstw;i++){
				// vertical linear interpolation && yuv2rgb in a single step:
				int Y=yuvtab_2568[buf0[i]>>7];
				int U=((uvbuf0[i/2]*uvalpha1+uvbuf1[i/2]*uvalpha)>>19);
				int V=((uvbuf0[i/2+2048]*uvalpha1+uvbuf1[i/2+2048]*uvalpha)>>19);
				dest[0]=clip_table[((Y + yuvtab_40cf[U]) >>13)];
				dest[1]=clip_table[((Y + yuvtab_1a1e[V] + yuvtab_0c92[U]) >>13)];
				dest[2]=clip_table[((Y + yuvtab_3343[V]) >>13)];
				dest+=dstbpp>>3;
			}
		}
		else if(dstbpp==16)
		{
			for(i=0;i<dstw;i++){
				// vertical linear interpolation && yuv2rgb in a single step:
				int Y=yuvtab_2568[buf0[i]>>7];
				int U=((uvbuf0[i/2]*uvalpha1+uvbuf1[i/2]*uvalpha)>>19);
				int V=((uvbuf0[i/2+2048]*uvalpha1+uvbuf1[i/2+2048]*uvalpha)>>19);

				((uint16_t*)dest)[0] =
					(clip_table[(Y + yuvtab_40cf[U]) >>13]>>3) |
					((clip_table[(Y + yuvtab_1a1e[V] + yuvtab_0c92[U]) >>13]<<3)&0x07E0) |
					((clip_table[(Y + yuvtab_3343[V]) >>13]<<8)&0xF800);
				dest+=2;
			}
		}
		else if(dstbpp==15)
		{
			for(i=0;i<dstw;i++){
				// vertical linear interpolation && yuv2rgb in a single step:
				int Y=yuvtab_2568[buf0[i]>>7];
				int U=((uvbuf0[i/2]*uvalpha1+uvbuf1[i/2]*uvalpha)>>19);
				int V=((uvbuf0[i/2+2048]*uvalpha1+uvbuf1[i/2+2048]*uvalpha)>>19);

				((uint16_t*)dest)[0] =
					(clip_table[(Y + yuvtab_40cf[U]) >>13]>>3) |
					((clip_table[(Y + yuvtab_1a1e[V] + yuvtab_0c92[U]) >>13]<<2)&0x03E0) |
					((clip_table[(Y + yuvtab_3343[V]) >>13]<<7)&0x7C00);
				dest+=2;
			}
		}
#endif
}


static inline void hyscale(uint16_t *dst, int dstWidth, uint8_t *src, int srcWidth, int xInc)
{
	int i;
      unsigned int xpos=0;
      // *** horizontal scale Y line to temp buffer
#ifdef ARCH_X86

#ifdef HAVE_MMX2
	if(canMMX2BeUsed)
	{
		asm volatile(
			"pxor %%mm7, %%mm7		\n\t"
			"pxor %%mm2, %%mm2		\n\t" // 2*xalpha
			"movd %5, %%mm6			\n\t" // xInc&0xFFFF
			"punpcklwd %%mm6, %%mm6		\n\t"
			"punpcklwd %%mm6, %%mm6		\n\t"
			"movq %%mm6, %%mm2		\n\t"
			"psllq $16, %%mm2		\n\t"
			"paddw %%mm6, %%mm2		\n\t"
			"psllq $16, %%mm2		\n\t"
			"paddw %%mm6, %%mm2		\n\t"
			"psllq $16, %%mm2		\n\t" //0,t,2t,3t		t=xInc&0xFF
			"movq %%mm2, temp0		\n\t"
			"movd %4, %%mm6			\n\t" //(xInc*4)&0xFFFF
			"punpcklwd %%mm6, %%mm6		\n\t"
			"punpcklwd %%mm6, %%mm6		\n\t"
			"xorl %%eax, %%eax		\n\t" // i
			"movl %0, %%esi			\n\t" // src
			"movl %1, %%edi			\n\t" // buf1
			"movl %3, %%edx			\n\t" // (xInc*4)>>16
			"xorl %%ecx, %%ecx		\n\t"
			"xorl %%ebx, %%ebx		\n\t"
			"movw %4, %%bx			\n\t" // (xInc*4)&0xFFFF
#ifdef HAVE_MMX2
#define FUNNY_Y_CODE \
			"prefetchnta 1024(%%esi)	\n\t"\
			"prefetchnta 1056(%%esi)	\n\t"\
			"prefetchnta 1088(%%esi)	\n\t"\
			"call funnyYCode		\n\t"\
			"movq temp0, %%mm2		\n\t"\
			"xorl %%ecx, %%ecx		\n\t"
#else
#define FUNNY_Y_CODE \
			"call funnyYCode		\n\t"\
			"movq temp0, %%mm2		\n\t"\
			"xorl %%ecx, %%ecx		\n\t"
#endif
FUNNY_Y_CODE
FUNNY_Y_CODE
FUNNY_Y_CODE
FUNNY_Y_CODE
FUNNY_Y_CODE
FUNNY_Y_CODE
FUNNY_Y_CODE
FUNNY_Y_CODE

			:: "m" (src), "m" (dst), "m" (dstWidth), "m" ((xInc*4)>>16),
			"m" ((xInc*4)&0xFFFF), "m" (xInc&0xFFFF)
			: "%eax", "%ebx", "%ecx", "%edx", "%esi", "%edi"
		);
		for(i=dstWidth-1; (i*xInc)>>16 >=srcWidth-1; i--) dst[i] = src[srcWidth-1]*128;
	}
	else
	{
#endif
	//NO MMX just normal asm ...
	asm volatile(
		"xorl %%eax, %%eax		\n\t" // i
		"xorl %%ebx, %%ebx		\n\t" // xx
		"xorl %%ecx, %%ecx		\n\t" // 2*xalpha
		"1:				\n\t"
		"movzbl  (%0, %%ebx), %%edi	\n\t" //src[xx]
		"movzbl 1(%0, %%ebx), %%esi	\n\t" //src[xx+1]
		"subl %%edi, %%esi		\n\t" //src[xx+1] - src[xx]
		"imull %%ecx, %%esi		\n\t" //(src[xx+1] - src[xx])*2*xalpha
		"shll $16, %%edi		\n\t"
		"addl %%edi, %%esi		\n\t" //src[xx+1]*2*xalpha + src[xx]*(1-2*xalpha)
		"movl %1, %%edi			\n\t"
		"shrl $9, %%esi			\n\t"
		"movw %%si, (%%edi, %%eax, 2)	\n\t"
		"addw %4, %%cx			\n\t" //2*xalpha += xInc&0xFF
		"adcl %3, %%ebx			\n\t" //xx+= xInc>>8 + carry

		"movzbl (%0, %%ebx), %%edi	\n\t" //src[xx]
		"movzbl 1(%0, %%ebx), %%esi	\n\t" //src[xx+1]
		"subl %%edi, %%esi		\n\t" //src[xx+1] - src[xx]
		"imull %%ecx, %%esi		\n\t" //(src[xx+1] - src[xx])*2*xalpha
		"shll $16, %%edi		\n\t"
		"addl %%edi, %%esi		\n\t" //src[xx+1]*2*xalpha + src[xx]*(1-2*xalpha)
		"movl %1, %%edi			\n\t"
		"shrl $9, %%esi			\n\t"
		"movw %%si, 2(%%edi, %%eax, 2)	\n\t"
		"addw %4, %%cx			\n\t" //2*xalpha += xInc&0xFF
		"adcl %3, %%ebx			\n\t" //xx+= xInc>>8 + carry


		"addl $2, %%eax			\n\t"
		"cmpl %2, %%eax			\n\t"
		" jb 1b				\n\t"


		:: "r" (src), "m" (dst), "m" (dstWidth), "m" (xInc>>16), "m" (xInc&0xFFFF)
		: "%eax", "%ebx", "%ecx", "%edi", "%esi"
		);
#ifdef HAVE_MMX2
	} //if MMX2 cant be used
#endif
#else
      for(i=0;i<dstWidth;i++){
	register unsigned int xx=xpos>>16;
        register unsigned int xalpha=(xpos&0xFFFF)>>9;
	dst[i]=(src[xx]*(xalpha^127)+src[xx+1]*xalpha);
	xpos+=xInc;
      }
#endif
}

inline static void hcscale(uint16_t *dst, int dstWidth,
				uint8_t *src1, uint8_t *src2, int srcWidth, int xInc)
{
	int xpos=0;
	int i;
#ifdef ARCH_X86
#ifdef HAVE_MMX2
	if(canMMX2BeUsed)
	{
		asm volatile(
		"pxor %%mm7, %%mm7		\n\t"
		"pxor %%mm2, %%mm2		\n\t" // 2*xalpha
		"movd %5, %%mm6			\n\t" // xInc&0xFFFF
		"punpcklwd %%mm6, %%mm6		\n\t"
		"punpcklwd %%mm6, %%mm6		\n\t"
		"movq %%mm6, %%mm2		\n\t"
		"psllq $16, %%mm2		\n\t"
		"paddw %%mm6, %%mm2		\n\t"
		"psllq $16, %%mm2		\n\t"
		"paddw %%mm6, %%mm2		\n\t"
		"psllq $16, %%mm2		\n\t" //0,t,2t,3t		t=xInc&0xFFFF
		"movq %%mm2, temp0		\n\t"
		"movd %4, %%mm6			\n\t" //(xInc*4)&0xFFFF
		"punpcklwd %%mm6, %%mm6		\n\t"
		"punpcklwd %%mm6, %%mm6		\n\t"
		"xorl %%eax, %%eax		\n\t" // i
		"movl %0, %%esi			\n\t" // src
		"movl %1, %%edi			\n\t" // buf1
		"movl %3, %%edx			\n\t" // (xInc*4)>>16
		"xorl %%ecx, %%ecx		\n\t"
		"xorl %%ebx, %%ebx		\n\t"
		"movw %4, %%bx			\n\t" // (xInc*4)&0xFFFF

#ifdef HAVE_MMX2
#define FUNNYUVCODE \
			"prefetchnta 1024(%%esi)	\n\t"\
			"prefetchnta 1056(%%esi)	\n\t"\
			"prefetchnta 1088(%%esi)	\n\t"\
			"call funnyUVCode		\n\t"\
			"movq temp0, %%mm2		\n\t"\
			"xorl %%ecx, %%ecx		\n\t"
#else
#define FUNNYUVCODE \
			"call funnyUVCode		\n\t"\
			"movq temp0, %%mm2		\n\t"\
			"xorl %%ecx, %%ecx		\n\t"
#endif

FUNNYUVCODE
FUNNYUVCODE
FUNNYUVCODE
FUNNYUVCODE

FUNNYUVCODE
FUNNYUVCODE
FUNNYUVCODE
FUNNYUVCODE


		"xorl %%eax, %%eax		\n\t" // i
		"movl %6, %%esi			\n\t" // src
		"movl %1, %%edi			\n\t" // buf1
		"addl $4096, %%edi		\n\t"

FUNNYUVCODE
FUNNYUVCODE
FUNNYUVCODE
FUNNYUVCODE

FUNNYUVCODE
FUNNYUVCODE
FUNNYUVCODE
FUNNYUVCODE

		:: "m" (src1), "m" (dst), "m" (dstWidth), "m" ((xInc*4)>>16),
		  "m" ((xInc*4)&0xFFFF), "m" (xInc&0xFFFF), "m" (src2)
		: "%eax", "%ebx", "%ecx", "%edx", "%esi", "%edi"
	);
		for(i=dstWidth-1; (i*xInc)>>16 >=srcWidth/2-1; i--)
		{
			dst[i] = src1[srcWidth/2-1]*128;
			dst[i+2048] = src2[srcWidth/2-1]*128;
		}
	}
	else
	{
#endif
	asm volatile(
		"xorl %%eax, %%eax		\n\t" // i
		"xorl %%ebx, %%ebx		\n\t" // xx
		"xorl %%ecx, %%ecx		\n\t" // 2*xalpha
		"1:				\n\t"
		"movl %0, %%esi			\n\t"
		"movzbl  (%%esi, %%ebx), %%edi	\n\t" //src[xx]
		"movzbl 1(%%esi, %%ebx), %%esi	\n\t" //src[xx+1]
		"subl %%edi, %%esi		\n\t" //src[xx+1] - src[xx]
		"imull %%ecx, %%esi		\n\t" //(src[xx+1] - src[xx])*2*xalpha
		"shll $16, %%edi		\n\t"
		"addl %%edi, %%esi		\n\t" //src[xx+1]*2*xalpha + src[xx]*(1-2*xalpha)
		"movl %1, %%edi			\n\t"
		"shrl $9, %%esi			\n\t"
		"movw %%si, (%%edi, %%eax, 2)	\n\t"

		"movzbl  (%5, %%ebx), %%edi	\n\t" //src[xx]
		"movzbl 1(%5, %%ebx), %%esi	\n\t" //src[xx+1]
		"subl %%edi, %%esi		\n\t" //src[xx+1] - src[xx]
		"imull %%ecx, %%esi		\n\t" //(src[xx+1] - src[xx])*2*xalpha
		"shll $16, %%edi		\n\t"
		"addl %%edi, %%esi		\n\t" //src[xx+1]*2*xalpha + src[xx]*(1-2*xalpha)
		"movl %1, %%edi			\n\t"
		"shrl $9, %%esi			\n\t"
		"movw %%si, 4096(%%edi, %%eax, 2)\n\t"

		"addw %4, %%cx			\n\t" //2*xalpha += xInc&0xFF
		"adcl %3, %%ebx			\n\t" //xx+= xInc>>8 + carry
		"addl $1, %%eax			\n\t"
		"cmpl %2, %%eax			\n\t"
		" jb 1b				\n\t"

		:: "m" (src1), "m" (dst), "m" (dstWidth), "m" (xInc>>16), "m" (xInc&0xFFFF),
		"r" (src2)
		: "%eax", "%ebx", "%ecx", "%edi", "%esi"
		);
#ifdef HAVE_MMX2
	} //if MMX2 cant be used
#endif
#else
      for(i=0;i<dstWidth;i++){
	  register unsigned int xx=xpos>>16;
          register unsigned int xalpha=(xpos&0xFFFF)>>9;
	  dst[i]=(src1[xx]*(xalpha^127)+src1[xx+1]*xalpha);
	  dst[i+2048]=(src2[xx]*(xalpha^127)+src2[xx+1]*xalpha);
	  xpos+=xInc;
      }
#endif
}


// *** bilinear scaling and yuv->rgb or yuv->yuv conversion of yv12 slices:
// *** Note: it's called multiple times while decoding a frame, first time y==0
// *** Designed to upscale, but may work for downscale too.
// s_xinc = (src_width << 16) / dst_width
// s_yinc = (src_height << 16) / dst_height
void SwScale_YV12slice(unsigned char* srcptr[],int stride[], int y, int h,
			     uint8_t* dstptr[], int dststride, int dstw, int dstbpp,
			     unsigned int s_xinc,unsigned int s_yinc){

// scaling factors:
//static int s_yinc=(vo_dga_src_height<<16)/vo_dga_vp_height;
//static int s_xinc=(vo_dga_src_width<<8)/vo_dga_vp_width;

unsigned int s_xinc2;

static int s_srcypos; // points to the dst Pixels center in the source (0 is the center of pixel 0,0 in src)
static int s_ypos;

// last horzontally interpolated lines, used to avoid unnecessary calculations
static int s_last_ypos;
static int s_last_y1pos;

static int static_dstw;

#ifdef HAVE_MMX2
// used to detect a horizontal size change
static int old_dstw= -1;
static int old_s_xinc= -1;
#endif

int srcWidth= (dstw*s_xinc + 0x8000)>>16;
int dstUVw= fullUVIpol ? dstw : dstw/2;


#ifdef HAVE_MMX2
canMMX2BeUsed= (s_xinc <= 0x10000 && (dstw&31)==0 && (srcWidth&15)==0) ? 1 : 0;
#endif

// match pixel 0 of the src to pixel 0 of dst and match pixel n-2 of src to pixel n-2 of dst
// n-2 is the last chrominance sample available
// FIXME this is not perfect, but noone shuld notice the difference, the more correct variant
// would be like the vertical one, but that would require some special code for the
// first and last pixel
if(canMMX2BeUsed) 	s_xinc+= 20;
else			s_xinc = ((srcWidth-2)<<16)/(dstw-2) - 20;

if(fullUVIpol && !dstbpp==12) 	s_xinc2= s_xinc>>1;
else				s_xinc2= s_xinc;
  // force calculation of the horizontal interpolation of the first line

  if(y==0){
	s_last_ypos=-99;
	s_last_y1pos=-99;
	s_srcypos= s_yinc/2 - 0x8000;
	s_ypos=0;
#ifdef HAVE_MMX2
// cant downscale !!!
	if((old_s_xinc != s_xinc || old_dstw!=dstw) && canMMX2BeUsed)
	{
		uint8_t *fragment;
		int imm8OfPShufW1;
		int imm8OfPShufW2;
		int fragmentLength;

		int xpos, xx, xalpha, i;

		old_s_xinc= s_xinc;
		old_dstw= dstw;

		static_dstw= dstw;

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
			"addw %%bx, %%cx		\n\t" //2*xalpha += (4*s_xinc)&0xFFFF
			"pshufw $0xFF, %%mm1, %%mm1	\n\t"
			"1:				\n\t"
			"adcl %%edx, %%esi		\n\t" //xx+= (4*s_xinc)>>16 + carry
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

		xpos= 0; //s_xinc/2 - 0x8000; // difference between pixel centers

		/* choose xinc so that all 8 parts fit exactly
		   Note: we cannot use just 1 part because it would not fit in the code cache */
//		s_xinc2_diff= -((((s_xinc2*(dstw/8))&0xFFFF))/(dstw/8))-10;
//		s_xinc_diff= -((((s_xinc*(dstw/8))&0xFFFF))/(dstw/8));
#ifdef ALT_ERROR
//		s_xinc2_diff+= ((0x10000/(dstw/8)));
#endif
//		s_xinc_diff= s_xinc2_diff*2;

//		s_xinc2+= s_xinc2_diff;
//		s_xinc+= s_xinc_diff;

//		old_s_xinc= s_xinc;

		for(i=0; i<dstw/8; i++)
		{
			int xx=xpos>>16;

			if((i&3) == 0)
			{
				int a=0;
				int b=((xpos+s_xinc)>>16) - xx;
				int c=((xpos+s_xinc*2)>>16) - xx;
				int d=((xpos+s_xinc*3)>>16) - xx;

				memcpy(funnyYCode + fragmentLength*i/4, fragment, fragmentLength);

				funnyYCode[fragmentLength*i/4 + imm8OfPShufW1]=
				funnyYCode[fragmentLength*i/4 + imm8OfPShufW2]=
					a | (b<<2) | (c<<4) | (d<<6);

				// if we dont need to read 8 bytes than dont :), reduces the chance of
				// crossing a cache line
				if(d<3) funnyYCode[fragmentLength*i/4 + 1]= 0x6E;

				funnyYCode[fragmentLength*(i+4)/4]= RET;
			}
			xpos+=s_xinc;
		}

		xpos= 0; //s_xinc2/2 - 0x10000; // difference between centers of chrom samples
		for(i=0; i<dstUVw/8; i++)
		{
			int xx=xpos>>16;

			if((i&3) == 0)
			{
				int a=0;
				int b=((xpos+s_xinc2)>>16) - xx;
				int c=((xpos+s_xinc2*2)>>16) - xx;
				int d=((xpos+s_xinc2*3)>>16) - xx;

				memcpy(funnyUVCode + fragmentLength*i/4, fragment, fragmentLength);

				funnyUVCode[fragmentLength*i/4 + imm8OfPShufW1]=
				funnyUVCode[fragmentLength*i/4 + imm8OfPShufW2]=
					a | (b<<2) | (c<<4) | (d<<6);

				// if we dont need to read 8 bytes than dont :), reduces the chance of
				// crossing a cache line
				if(d<3) funnyUVCode[fragmentLength*i/4 + 1]= 0x6E;

				funnyUVCode[fragmentLength*(i+4)/4]= RET;
			}
			xpos+=s_xinc2;
		}
//		funnyCode[0]= RET;
	}

#endif // HAVE_MMX2
  } // reset counters

  while(1){
    unsigned char *dest =dstptr[0]+dststride*s_ypos;
    unsigned char *uDest=dstptr[1]+(dststride>>1)*(s_ypos>>1);
    unsigned char *vDest=dstptr[2]+(dststride>>1)*(s_ypos>>1);

    int y0=(s_srcypos + 0xFFFF)>>16;  // first luminance source line number below the dst line
	// points to the dst Pixels center in the source (0 is the center of pixel 0,0 in src)
    int srcuvpos= dstbpp==12 ?	s_srcypos + s_yinc/2 - 0x8000 :
    				s_srcypos - 0x8000;
    int y1=(srcuvpos + 0x1FFFF)>>17; // first chrominance source line number below the dst line
    int yalpha=((s_srcypos-1)&0xFFFF)>>4;
    int uvalpha=((srcuvpos-1)&0x1FFFF)>>5;
    uint16_t *buf0=pix_buf_y[y0&1];		// top line of the interpolated slice
    uint16_t *buf1=pix_buf_y[((y0+1)&1)];	// bottom line of the interpolated slice
    uint16_t *uvbuf0=pix_buf_uv[y1&1];		// top line of the interpolated slice
    uint16_t *uvbuf1=pix_buf_uv[(y1+1)&1];	// bottom line of the interpolated slice
    int i;

    if(y0>=y+h) break; // FIXME wrong, skips last lines, but they are dupliactes anyway

    if((y0&1) && dstbpp==12) uvalpha=-1; // there is no alpha if there is no line

    s_ypos++; s_srcypos+=s_yinc;

    //only interpolate the src line horizontally if we didnt do it allready
	if(s_last_ypos!=y0)
	{
		unsigned char *src;
		// skip if first line has been horiz scaled alleady
		if(s_last_ypos != y0-1)
		{
			// check if first line is before any available src lines
			if(y0-1 < y) 	src=srcptr[0]+(0     )*stride[0];
			else		src=srcptr[0]+(y0-y-1)*stride[0];

			hyscale(buf0, dstw, src, srcWidth, s_xinc);
		}
		// check if second line is after any available src lines
		if(y0-y >= h)	src=srcptr[0]+(h-1)*stride[0];
		else		src=srcptr[0]+(y0-y)*stride[0];

		// the min() is required to avoid reuseing lines which where not available
		s_last_ypos= MIN(y0, y+h-1);
		hyscale(buf1, dstw, src, srcWidth, s_xinc);
	}
//	printf("%d %d %d %d\n", y, y1, s_last_y1pos, h);
      // *** horizontal scale U and V lines to temp buffer
	if(s_last_y1pos!=y1)
	{
		uint8_t *src1, *src2;
		// skip if first line has been horiz scaled alleady
		if(s_last_y1pos != y1-1)
		{
			// check if first line is before any available src lines
			if(y1-y/2-1 < 0)
			{
				src1= srcptr[1]+(0)*stride[1];
				src2= srcptr[2]+(0)*stride[2];
			}else{
				src1= srcptr[1]+(y1-y/2-1)*stride[1];
				src2= srcptr[2]+(y1-y/2-1)*stride[2];
			}
			hcscale(uvbuf0, dstUVw, src1, src2, srcWidth, s_xinc2);
		}

		// check if second line is after any available src lines
		if(y1 - y/2 >= h/2)
		{
			src1= srcptr[1]+(h/2-1)*stride[1];
			src2= srcptr[2]+(h/2-1)*stride[2];
		}else{
			src1= srcptr[1]+(y1-y/2)*stride[1];
			src2= srcptr[2]+(y1-y/2)*stride[2];
		}
		hcscale(uvbuf1, dstUVw, src1, src2, srcWidth, s_xinc2);

		// the min() is required to avoid reuseing lines which where not available
		s_last_y1pos= MIN(y1, y/2+h/2-1);
	}

	if(dstbpp==12) //YV12
		yuv2yuv(buf0, buf1, uvbuf0, uvbuf1, dest, uDest, vDest, dstw, yalpha, uvalpha);
	else if(ABS(s_yinc - 0x10000) < 10)
		yuv2rgb1(buf0, buf1, uvbuf0, uvbuf1, dest, dstw, yalpha, uvalpha, dstbpp);
	else
		yuv2rgbX(buf0, buf1, uvbuf0, uvbuf1, dest, dstw, yalpha, uvalpha, dstbpp);

#ifdef HAVE_MMX
    	b16Dither= b16Dither1;
	b16Dither1= b16Dither2;
	b16Dither2= b16Dither;

	g16Dither= g16Dither1;
	g16Dither1= g16Dither2;
	g16Dither2= g16Dither;
#endif
  }

#ifdef HAVE_3DNOW
	asm volatile("femms");
#elif defined (HAVE_MMX)
	asm volatile("emms");
#endif
}


void SwScale_Init(){
    // generating tables:
    int i;
    for(i=0;i<256;i++){
        clip_table[i]=0;
        clip_table[i+256]=i;
        clip_table[i+512]=255;
	yuvtab_2568[i]=(0x2568*(i-16))+(256<<13);
	yuvtab_3343[i]=0x3343*(i-128);
	yuvtab_0c92[i]=-0x0c92*(i-128);
	yuvtab_1a1e[i]=-0x1a1e*(i-128);
	yuvtab_40cf[i]=0x40cf*(i-128);
    }

}
