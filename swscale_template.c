
// Software scaling and colorspace conversion routines for MPlayer

// Orginal C implementation by A'rpi/ESP-team <arpi@thot.banki.hu>
// current version mostly by Michael Niedermayer (michaelni@gmx.at)
// the parts written by michael are under GNU GPL

/* TODO
Move static / global vars into a struct so multiple scalers can be used
write vertical cubic upscale / linear downscale stuff
*/

#undef MOVNTQ
#undef PAVGB
#undef PREFETCH
#undef PREFETCHW
#undef EMMS
#undef SFENCE

#ifdef HAVE_3DNOW
/* On K6 femms is faster of emms. On K7 femms is directly mapped on emms. */
#define EMMS     "femms"
#else
#define EMMS     "emms"
#endif

#ifdef HAVE_3DNOW
#define PREFETCH  "prefetch"
#define PREFETCHW "prefetchw"
#elif defined ( HAVE_MMX2 )
#define PREFETCH "prefetchnta"
#define PREFETCHW "prefetcht0"
#else
#define PREFETCH "/nop"
#define PREFETCHW "/nop"
#endif

#ifdef HAVE_MMX2
#define SFENCE "sfence"
#else
#define SFENCE "/nop"
#endif

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

#define FULL_YSCALEYUV2RGB \
		"pxor %%mm7, %%mm7		\n\t"\
		"movd %6, %%mm6			\n\t" /*yalpha1*/\
		"punpcklwd %%mm6, %%mm6		\n\t"\
		"punpcklwd %%mm6, %%mm6		\n\t"\
		"movd %7, %%mm5			\n\t" /*uvalpha1*/\
		"punpcklwd %%mm5, %%mm5		\n\t"\
		"punpcklwd %%mm5, %%mm5		\n\t"\
		"xorl %%eax, %%eax		\n\t"\
		".balign 16			\n\t"\
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
		".balign 16			\n\t"\
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
		".balign 16			\n\t"\
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
		"movq (%0, %%eax, 2), %%mm1	\n\t" /*buf0[eax]*/\
		"movq 8(%0, %%eax, 2), %%mm7	\n\t" /*buf0[eax]*/\
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

// do vertical chrominance interpolation
#define YSCALEYUV2RGB1b \
		"xorl %%eax, %%eax		\n\t"\
		".balign 16			\n\t"\
		"1:				\n\t"\
		"movq (%2, %%eax), %%mm2	\n\t" /* uvbuf0[eax]*/\
		"movq (%3, %%eax), %%mm3	\n\t" /* uvbuf1[eax]*/\
		"movq 4096(%2, %%eax), %%mm5	\n\t" /* uvbuf0[eax+2048]*/\
		"movq 4096(%3, %%eax), %%mm4	\n\t" /* uvbuf1[eax+2048]*/\
		"paddw %%mm2, %%mm3		\n\t" /* uvbuf0[eax] + uvbuf1[eax]*/\
		"paddw %%mm5, %%mm4		\n\t" /* uvbuf0[eax+2048] + uvbuf1[eax+2048]*/\
		"psrlw $5, %%mm3		\n\t"\
		"psrlw $5, %%mm4		\n\t"\
		"psubw w400, %%mm3		\n\t" /* (U-128)8*/\
		"psubw w400, %%mm4		\n\t" /* (V-128)8*/\
		"movq %%mm3, %%mm2		\n\t" /* (U-128)8*/\
		"movq %%mm4, %%mm5		\n\t" /* (V-128)8*/\
		"pmulhw ugCoeff, %%mm3		\n\t"\
		"pmulhw vgCoeff, %%mm4		\n\t"\
	/* mm2=(U-128)8, mm3=ug, mm4=vg mm5=(V-128)8 */\
		"movq (%0, %%eax, 2), %%mm1	\n\t" /*buf0[eax]*/\
		"movq 8(%0, %%eax, 2), %%mm7	\n\t" /*buf0[eax]*/\
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
			"pand bF8, %%mm2		\n\t" /* B */\
			"pand bFC, %%mm4		\n\t" /* G */\
			"pand bF8, %%mm5		\n\t" /* R */\
			"psrlq $3, %%mm2		\n\t"\
\
			"movq %%mm2, %%mm1		\n\t"\
			"movq %%mm4, %%mm3		\n\t"\
\
			"punpcklbw %%mm7, %%mm3		\n\t"\
			"punpcklbw %%mm5, %%mm2		\n\t"\
			"punpckhbw %%mm7, %%mm4		\n\t"\
			"punpckhbw %%mm5, %%mm1		\n\t"\
\
			"psllq $3, %%mm3		\n\t"\
			"psllq $3, %%mm4		\n\t"\
\
			"por %%mm3, %%mm2		\n\t"\
			"por %%mm4, %%mm1		\n\t"\
\
			MOVNTQ(%%mm2, (%4, %%eax, 2))\
			MOVNTQ(%%mm1, 8(%4, %%eax, 2))\
\
			"addl $8, %%eax			\n\t"\
			"cmpl %5, %%eax			\n\t"\
			" jb 1b				\n\t"

#define WRITEBGR15 \
			"pand bF8, %%mm2		\n\t" /* B */\
			"pand bF8, %%mm4		\n\t" /* G */\
			"pand bF8, %%mm5		\n\t" /* R */\
			"psrlq $3, %%mm2		\n\t"\
			"psrlq $1, %%mm5		\n\t"\
\
			"movq %%mm2, %%mm1		\n\t"\
			"movq %%mm4, %%mm3		\n\t"\
\
			"punpcklbw %%mm7, %%mm3		\n\t"\
			"punpcklbw %%mm5, %%mm2		\n\t"\
			"punpckhbw %%mm7, %%mm4		\n\t"\
			"punpckhbw %%mm5, %%mm1		\n\t"\
\
			"psllq $2, %%mm3		\n\t"\
			"psllq $2, %%mm4		\n\t"\
\
			"por %%mm3, %%mm2		\n\t"\
			"por %%mm4, %%mm1		\n\t"\
\
			MOVNTQ(%%mm2, (%4, %%eax, 2))\
			MOVNTQ(%%mm1, 8(%4, %%eax, 2))\
\
			"addl $8, %%eax			\n\t"\
			"cmpl %5, %%eax			\n\t"\
			" jb 1b				\n\t"

#define WRITEBGR24OLD \
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
			MOVNTQ(%%mm0, (%%ebx))\
			MOVNTQ(%%mm2, 8(%%ebx))\
			MOVNTQ(%%mm3, 16(%%ebx))\
			"addl $24, %%ebx		\n\t"\
\
			"addl $8, %%eax			\n\t"\
			"cmpl %5, %%eax			\n\t"\
			" jb 1b				\n\t"

#define WRITEBGR24MMX \
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
			"movq %%mm2, %%mm6		\n\t" /* 0RGB0RGB 1 */\
			"movq %%mm1, %%mm5		\n\t" /* 0RGB0RGB 2 */\
			"movq %%mm3, %%mm7		\n\t" /* 0RGB0RGB 3 */\
\
			"psllq $40, %%mm0		\n\t" /* RGB00000 0 */\
			"psllq $40, %%mm2		\n\t" /* RGB00000 1 */\
			"psllq $40, %%mm1		\n\t" /* RGB00000 2 */\
			"psllq $40, %%mm3		\n\t" /* RGB00000 3 */\
\
			"punpckhdq %%mm4, %%mm0		\n\t" /* 0RGBRGB0 0 */\
			"punpckhdq %%mm6, %%mm2		\n\t" /* 0RGBRGB0 1 */\
			"punpckhdq %%mm5, %%mm1		\n\t" /* 0RGBRGB0 2 */\
			"punpckhdq %%mm7, %%mm3		\n\t" /* 0RGBRGB0 3 */\
\
			"psrlq $8, %%mm0		\n\t" /* 00RGBRGB 0 */\
			"movq %%mm2, %%mm6		\n\t" /* 0RGBRGB0 1 */\
			"psllq $40, %%mm2		\n\t" /* GB000000 1 */\
			"por %%mm2, %%mm0		\n\t" /* GBRGBRGB 0 */\
			MOVNTQ(%%mm0, (%%ebx))\
\
			"psrlq $24, %%mm6		\n\t" /* 0000RGBR 1 */\
			"movq %%mm1, %%mm5		\n\t" /* 0RGBRGB0 2 */\
			"psllq $24, %%mm1		\n\t" /* BRGB0000 2 */\
			"por %%mm1, %%mm6		\n\t" /* BRGBRGBR 1 */\
			MOVNTQ(%%mm6, 8(%%ebx))\
\
			"psrlq $40, %%mm5		\n\t" /* 000000RG 2 */\
			"psllq $8, %%mm3		\n\t" /* RGBRGB00 3 */\
			"por %%mm3, %%mm5		\n\t" /* RGBRGBRG 2 */\
			MOVNTQ(%%mm5, 16(%%ebx))\
\
			"addl $24, %%ebx		\n\t"\
\
			"addl $8, %%eax			\n\t"\
			"cmpl %5, %%eax			\n\t"\
			" jb 1b				\n\t"

#define WRITEBGR24MMX2 \
		/* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */\
			"movq M24A, %%mm0		\n\t"\
			"movq M24C, %%mm7		\n\t"\
			"pshufw $0x50, %%mm2, %%mm1	\n\t" /* B3 B2 B3 B2  B1 B0 B1 B0 */\
			"pshufw $0x50, %%mm4, %%mm3	\n\t" /* G3 G2 G3 G2  G1 G0 G1 G0 */\
			"pshufw $0x00, %%mm5, %%mm6	\n\t" /* R1 R0 R1 R0  R1 R0 R1 R0 */\
\
			"pand %%mm0, %%mm1		\n\t" /*    B2        B1       B0 */\
			"pand %%mm0, %%mm3		\n\t" /*    G2        G1       G0 */\
			"pand %%mm7, %%mm6		\n\t" /*       R1        R0       */\
\
			"psllq $8, %%mm3		\n\t" /* G2        G1       G0    */\
			"por %%mm1, %%mm6		\n\t"\
			"por %%mm3, %%mm6		\n\t"\
			MOVNTQ(%%mm6, (%%ebx))\
\
			"psrlq $8, %%mm4		\n\t" /* 00 G7 G6 G5  G4 G3 G2 G1 */\
			"pshufw $0xA5, %%mm2, %%mm1	\n\t" /* B5 B4 B5 B4  B3 B2 B3 B2 */\
			"pshufw $0x55, %%mm4, %%mm3	\n\t" /* G4 G3 G4 G3  G4 G3 G4 G3 */\
			"pshufw $0xA5, %%mm5, %%mm6	\n\t" /* R5 R4 R5 R4  R3 R2 R3 R2 */\
\
			"pand M24B, %%mm1		\n\t" /* B5       B4        B3    */\
			"pand %%mm7, %%mm3		\n\t" /*       G4        G3       */\
			"pand %%mm0, %%mm6		\n\t" /*    R4        R3       R2 */\
\
			"por %%mm1, %%mm3		\n\t" /* B5    G4 B4     G3 B3    */\
			"por %%mm3, %%mm6		\n\t"\
			MOVNTQ(%%mm6, 8(%%ebx))\
\
			"pshufw $0xFF, %%mm2, %%mm1	\n\t" /* B7 B6 B7 B6  B7 B6 B6 B7 */\
			"pshufw $0xFA, %%mm4, %%mm3	\n\t" /* 00 G7 00 G7  G6 G5 G6 G5 */\
			"pshufw $0xFA, %%mm5, %%mm6	\n\t" /* R7 R6 R7 R6  R5 R4 R5 R4 */\
\
			"pand %%mm7, %%mm1		\n\t" /*       B7        B6       */\
			"pand %%mm0, %%mm3		\n\t" /*    G7        G6       G5 */\
			"pand M24B, %%mm6		\n\t" /* R7       R6        R5    */\
\
			"por %%mm1, %%mm3		\n\t"\
			"por %%mm3, %%mm6		\n\t"\
			MOVNTQ(%%mm6, 16(%%ebx))\
\
			"addl $24, %%ebx		\n\t"\
\
			"addl $8, %%eax			\n\t"\
			"cmpl %5, %%eax			\n\t"\
			" jb 1b				\n\t"

#ifdef HAVE_MMX2
#undef WRITEBGR24
#define WRITEBGR24 WRITEBGR24MMX2
#else
#undef WRITEBGR24
#define WRITEBGR24 WRITEBGR24MMX
#endif

static inline void RENAME(yuv2yuv)(uint16_t *buf0, uint16_t *buf1, uint16_t *uvbuf0, uint16_t *uvbuf1,
			   uint8_t *dest, uint8_t *uDest, uint8_t *vDest, int dstW, int yalpha, int uvalpha)
{
	int yalpha1=yalpha^4095;
	int uvalpha1=uvalpha^4095;
	int i;

#ifdef ARCH_X86
	asm volatile ("\n\t"::: "memory");
#endif

	for(i=0;i<dstW;i++)
	{
		((uint8_t*)dest)[i] = (buf0[i]*yalpha1+buf1[i]*yalpha)>>19;
	}

	if(uvalpha != -1)
	{
		for(i=0; i<(dstW>>1); i++)
		{
			((uint8_t*)uDest)[i] = (uvbuf0[i]*uvalpha1+uvbuf1[i]*uvalpha)>>19;
			((uint8_t*)vDest)[i] = (uvbuf0[i+2048]*uvalpha1+uvbuf1[i+2048]*uvalpha)>>19;
		}
	}
}

/**
 * vertical scale YV12 to RGB
 */
static inline void RENAME(yuv2rgbX)(uint16_t *buf0, uint16_t *buf1, uint16_t *uvbuf0, uint16_t *uvbuf1,
			    uint8_t *dest, int dstW, int yalpha, int uvalpha, int dstbpp)
{
	int yalpha1=yalpha^4095;
	int uvalpha1=uvalpha^4095;

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


			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstW),
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

			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "m" (dest), "m" (dstW),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax", "%ebx"
			);
		}
		else if(dstbpp==15)
		{
			asm volatile(

FULL_YSCALEYUV2RGB
#ifdef DITHER1XBPP
			"paddusb g5Dither, %%mm1	\n\t"
			"paddusb r5Dither, %%mm0	\n\t"
			"paddusb b5Dither, %%mm3	\n\t"
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

			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstW),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax"
			);
		}
		else if(dstbpp==16)
		{
			asm volatile(

FULL_YSCALEYUV2RGB
#ifdef DITHER1XBPP
			"paddusb g6Dither, %%mm1	\n\t"
			"paddusb r5Dither, %%mm0	\n\t"
			"paddusb b5Dither, %%mm3	\n\t"
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

			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstW),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax"
			);
		}
#else
		asm volatile ("\n\t"::: "memory");

		if(dstbpp==32 || dstbpp==24)
		{
			int i;
			for(i=0;i<dstW;i++){
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
			int i;
			for(i=0;i<dstW;i++){
				// vertical linear interpolation && yuv2rgb in a single step:
				int Y=yuvtab_2568[((buf0[i]*yalpha1+buf1[i]*yalpha)>>19)];
				int U=((uvbuf0[i]*uvalpha1+uvbuf1[i]*uvalpha)>>19);
				int V=((uvbuf0[i+2048]*uvalpha1+uvbuf1[i+2048]*uvalpha)>>19);

				((uint16_t*)dest)[i] =
					clip_table16b[(Y + yuvtab_40cf[U]) >>13] |
					clip_table16g[(Y + yuvtab_1a1e[V] + yuvtab_0c92[U]) >>13] |
					clip_table16r[(Y + yuvtab_3343[V]) >>13];
			}
		}
		else if(dstbpp==15)
		{
			int i;
			for(i=0;i<dstW;i++){
				// vertical linear interpolation && yuv2rgb in a single step:
				int Y=yuvtab_2568[((buf0[i]*yalpha1+buf1[i]*yalpha)>>19)];
				int U=((uvbuf0[i]*uvalpha1+uvbuf1[i]*uvalpha)>>19);
				int V=((uvbuf0[i+2048]*uvalpha1+uvbuf1[i+2048]*uvalpha)>>19);

				((uint16_t*)dest)[i] =
					clip_table15b[(Y + yuvtab_40cf[U]) >>13] |
					clip_table15g[(Y + yuvtab_1a1e[V] + yuvtab_0c92[U]) >>13] |
					clip_table15r[(Y + yuvtab_3343[V]) >>13];
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

			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstW),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax"
			);
		}
		else if(dstbpp==24)
		{
			asm volatile(
				"movl %4, %%ebx			\n\t"
				YSCALEYUV2RGB
				WRITEBGR24

			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "m" (dest), "m" (dstW),
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
				"paddusb b5Dither, %%mm2	\n\t"
				"paddusb g5Dither, %%mm4	\n\t"
				"paddusb r5Dither, %%mm5	\n\t"
#endif

				WRITEBGR15

			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstW),
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
				"paddusb b5Dither, %%mm2	\n\t"
				"paddusb g6Dither, %%mm4	\n\t"
				"paddusb r5Dither, %%mm5	\n\t"
#endif

				WRITEBGR16

			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstW),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax"
			);
		}
#else
		asm volatile ("\n\t"::: "memory");

		if(dstbpp==32)
		{
			int i;
			for(i=0; i<dstW-1; i+=2){
				// vertical linear interpolation && yuv2rgb in a single step:
				int Y1=yuvtab_2568[((buf0[i]*yalpha1+buf1[i]*yalpha)>>19)];
				int Y2=yuvtab_2568[((buf0[i+1]*yalpha1+buf1[i+1]*yalpha)>>19)];
				int U=((uvbuf0[i>>1]*uvalpha1+uvbuf1[i>>1]*uvalpha)>>19);
				int V=((uvbuf0[(i>>1)+2048]*uvalpha1+uvbuf1[(i>>1)+2048]*uvalpha)>>19);

				int Cb= yuvtab_40cf[U];
				int Cg= yuvtab_1a1e[V] + yuvtab_0c92[U];
				int Cr= yuvtab_3343[V];

				dest[4*i+0]=clip_table[((Y1 + Cb) >>13)];
				dest[4*i+1]=clip_table[((Y1 + Cg) >>13)];
				dest[4*i+2]=clip_table[((Y1 + Cr) >>13)];

				dest[4*i+4]=clip_table[((Y2 + Cb) >>13)];
				dest[4*i+5]=clip_table[((Y2 + Cg) >>13)];
				dest[4*i+6]=clip_table[((Y2 + Cr) >>13)];
			}
		}
		if(dstbpp==24)
		{
			int i;
			for(i=0; i<dstW-1; i+=2){
				// vertical linear interpolation && yuv2rgb in a single step:
				int Y1=yuvtab_2568[((buf0[i]*yalpha1+buf1[i]*yalpha)>>19)];
				int Y2=yuvtab_2568[((buf0[i+1]*yalpha1+buf1[i+1]*yalpha)>>19)];
				int U=((uvbuf0[i>>1]*uvalpha1+uvbuf1[i>>1]*uvalpha)>>19);
				int V=((uvbuf0[(i>>1)+2048]*uvalpha1+uvbuf1[(i>>1)+2048]*uvalpha)>>19);

				int Cb= yuvtab_40cf[U];
				int Cg= yuvtab_1a1e[V] + yuvtab_0c92[U];
				int Cr= yuvtab_3343[V];

				dest[0]=clip_table[((Y1 + Cb) >>13)];
				dest[1]=clip_table[((Y1 + Cg) >>13)];
				dest[2]=clip_table[((Y1 + Cr) >>13)];

				dest[3]=clip_table[((Y2 + Cb) >>13)];
				dest[4]=clip_table[((Y2 + Cg) >>13)];
				dest[5]=clip_table[((Y2 + Cr) >>13)];
				dest+=6;
			}
		}
		else if(dstbpp==16)
		{
			int i;
			for(i=0; i<dstW-1; i+=2){
				// vertical linear interpolation && yuv2rgb in a single step:
				int Y1=yuvtab_2568[((buf0[i]*yalpha1+buf1[i]*yalpha)>>19)];
				int Y2=yuvtab_2568[((buf0[i+1]*yalpha1+buf1[i+1]*yalpha)>>19)];
				int U=((uvbuf0[i>>1]*uvalpha1+uvbuf1[i>>1]*uvalpha)>>19);
				int V=((uvbuf0[(i>>1)+2048]*uvalpha1+uvbuf1[(i>>1)+2048]*uvalpha)>>19);

				int Cb= yuvtab_40cf[U];
				int Cg= yuvtab_1a1e[V] + yuvtab_0c92[U];
				int Cr= yuvtab_3343[V];

				((uint16_t*)dest)[i] =
					clip_table16b[(Y1 + Cb) >>13] |
					clip_table16g[(Y1 + Cg) >>13] |
					clip_table16r[(Y1 + Cr) >>13];

				((uint16_t*)dest)[i+1] =
					clip_table16b[(Y2 + Cb) >>13] |
					clip_table16g[(Y2 + Cg) >>13] |
					clip_table16r[(Y2 + Cr) >>13];
			}
		}
		else if(dstbpp==15)
		{
			int i;
			for(i=0; i<dstW-1; i+=2){
				// vertical linear interpolation && yuv2rgb in a single step:
				int Y1=yuvtab_2568[((buf0[i]*yalpha1+buf1[i]*yalpha)>>19)];
				int Y2=yuvtab_2568[((buf0[i+1]*yalpha1+buf1[i+1]*yalpha)>>19)];
				int U=((uvbuf0[i>>1]*uvalpha1+uvbuf1[i>>1]*uvalpha)>>19);
				int V=((uvbuf0[(i>>1)+2048]*uvalpha1+uvbuf1[(i>>1)+2048]*uvalpha)>>19);

				int Cb= yuvtab_40cf[U];
				int Cg= yuvtab_1a1e[V] + yuvtab_0c92[U];
				int Cr= yuvtab_3343[V];

				((uint16_t*)dest)[i] =
					clip_table15b[(Y1 + Cb) >>13] |
					clip_table15g[(Y1 + Cg) >>13] |
					clip_table15r[(Y1 + Cr) >>13];

				((uint16_t*)dest)[i+1] =
					clip_table15b[(Y2 + Cb) >>13] |
					clip_table15g[(Y2 + Cg) >>13] |
					clip_table15r[(Y2 + Cr) >>13];
			}
		}
#endif
	} //!FULL_UV_IPOL
}

/**
 * YV12 to RGB without scaling or interpolating
 */
static inline void RENAME(yuv2rgb1)(uint16_t *buf0, uint16_t *buf1, uint16_t *uvbuf0, uint16_t *uvbuf1,
			    uint8_t *dest, int dstW, int yalpha, int uvalpha, int dstbpp)
{
	int uvalpha1=uvalpha^4095;
#ifdef HAVE_MMX
	int yalpha1=yalpha^4095;
#endif

	if(fullUVIpol || allwaysIpol)
	{
		RENAME(yuv2rgbX)(buf0, buf1, uvbuf0, uvbuf1, dest, dstW, yalpha, uvalpha, dstbpp);
		return;
	}
	if( yalpha > 2048 ) buf0 = buf1;

#ifdef HAVE_MMX
	if( uvalpha < 2048 ) // note this is not correct (shifts chrominance by 0.5 pixels) but its a bit faster
	{
		if(dstbpp == 32)
		{
			asm volatile(
				YSCALEYUV2RGB1
				WRITEBGR32
			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstW),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax"
			);
		}
		else if(dstbpp==24)
		{
			asm volatile(
				"movl %4, %%ebx			\n\t"
				YSCALEYUV2RGB1
				WRITEBGR24
			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "m" (dest), "m" (dstW),
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
				"paddusb b5Dither, %%mm2	\n\t"
				"paddusb g5Dither, %%mm4	\n\t"
				"paddusb r5Dither, %%mm5	\n\t"
#endif
				WRITEBGR15
			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstW),
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
				"paddusb b5Dither, %%mm2	\n\t"
				"paddusb g6Dither, %%mm4	\n\t"
				"paddusb r5Dither, %%mm5	\n\t"
#endif

				WRITEBGR16
			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstW),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax"
			);
		}
	}
	else
	{
		if(dstbpp == 32)
		{
			asm volatile(
				YSCALEYUV2RGB1b
				WRITEBGR32
			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstW),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax"
			);
		}
		else if(dstbpp==24)
		{
			asm volatile(
				"movl %4, %%ebx			\n\t"
				YSCALEYUV2RGB1b
				WRITEBGR24
			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "m" (dest), "m" (dstW),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax", "%ebx"
			);
		}
		else if(dstbpp==15)
		{
			asm volatile(
				YSCALEYUV2RGB1b
		/* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
				"paddusb b5Dither, %%mm2	\n\t"
				"paddusb g5Dither, %%mm4	\n\t"
				"paddusb r5Dither, %%mm5	\n\t"
#endif
				WRITEBGR15
			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstW),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax"
			);
		}
		else if(dstbpp==16)
		{
			asm volatile(
				YSCALEYUV2RGB1b
		/* mm2=B, %%mm4=G, %%mm5=R, %%mm7=0 */
#ifdef DITHER1XBPP
				"paddusb b5Dither, %%mm2	\n\t"
				"paddusb g6Dither, %%mm4	\n\t"
				"paddusb r5Dither, %%mm5	\n\t"
#endif

				WRITEBGR16
			:: "r" (buf0), "r" (buf1), "r" (uvbuf0), "r" (uvbuf1), "r" (dest), "m" (dstW),
			"m" (yalpha1), "m" (uvalpha1)
			: "%eax"
			);
		}
	}
#else
//FIXME write 2 versions (for even & odd lines)
	asm volatile ("\n\t"::: "memory");

	if(dstbpp==32)
	{
		int i;
		for(i=0; i<dstW-1; i+=2){
			// vertical linear interpolation && yuv2rgb in a single step:
			int Y1=yuvtab_2568[buf0[i]>>7];
			int Y2=yuvtab_2568[buf0[i+1]>>7];
			int U=((uvbuf0[i>>1]*uvalpha1+uvbuf1[i>>1]*uvalpha)>>19);
			int V=((uvbuf0[(i>>1)+2048]*uvalpha1+uvbuf1[(i>>1)+2048]*uvalpha)>>19);

			int Cb= yuvtab_40cf[U];
			int Cg= yuvtab_1a1e[V] + yuvtab_0c92[U];
			int Cr= yuvtab_3343[V];

			dest[4*i+0]=clip_table[((Y1 + Cb) >>13)];
			dest[4*i+1]=clip_table[((Y1 + Cg) >>13)];
			dest[4*i+2]=clip_table[((Y1 + Cr) >>13)];

			dest[4*i+4]=clip_table[((Y2 + Cb) >>13)];
			dest[4*i+5]=clip_table[((Y2 + Cg) >>13)];
			dest[4*i+6]=clip_table[((Y2 + Cr) >>13)];
		}
	}
	if(dstbpp==24)
	{
		int i;
		for(i=0; i<dstW-1; i+=2){
			// vertical linear interpolation && yuv2rgb in a single step:
			int Y1=yuvtab_2568[buf0[i]>>7];
			int Y2=yuvtab_2568[buf0[i+1]>>7];
			int U=((uvbuf0[i>>1]*uvalpha1+uvbuf1[i>>1]*uvalpha)>>19);
			int V=((uvbuf0[(i>>1)+2048]*uvalpha1+uvbuf1[(i>>1)+2048]*uvalpha)>>19);

			int Cb= yuvtab_40cf[U];
			int Cg= yuvtab_1a1e[V] + yuvtab_0c92[U];
			int Cr= yuvtab_3343[V];

			dest[0]=clip_table[((Y1 + Cb) >>13)];
			dest[1]=clip_table[((Y1 + Cg) >>13)];
			dest[2]=clip_table[((Y1 + Cr) >>13)];

			dest[3]=clip_table[((Y2 + Cb) >>13)];
			dest[4]=clip_table[((Y2 + Cg) >>13)];
			dest[5]=clip_table[((Y2 + Cr) >>13)];
			dest+=6;
		}
	}
	else if(dstbpp==16)
	{
		int i;
		for(i=0; i<dstW-1; i+=2){
			// vertical linear interpolation && yuv2rgb in a single step:
			int Y1=yuvtab_2568[buf0[i]>>7];
			int Y2=yuvtab_2568[buf0[i+1]>>7];
			int U=((uvbuf0[i>>1]*uvalpha1+uvbuf1[i>>1]*uvalpha)>>19);
			int V=((uvbuf0[(i>>1)+2048]*uvalpha1+uvbuf1[(i>>1)+2048]*uvalpha)>>19);

			int Cb= yuvtab_40cf[U];
			int Cg= yuvtab_1a1e[V] + yuvtab_0c92[U];
			int Cr= yuvtab_3343[V];

			((uint16_t*)dest)[i] =
				clip_table16b[(Y1 + Cb) >>13] |
				clip_table16g[(Y1 + Cg) >>13] |
				clip_table16r[(Y1 + Cr) >>13];

			((uint16_t*)dest)[i+1] =
				clip_table16b[(Y2 + Cb) >>13] |
				clip_table16g[(Y2 + Cg) >>13] |
				clip_table16r[(Y2 + Cr) >>13];
		}
	}
	else if(dstbpp==15)
	{
		int i;
		for(i=0; i<dstW-1; i+=2){
			// vertical linear interpolation && yuv2rgb in a single step:
			int Y1=yuvtab_2568[buf0[i]>>7];
			int Y2=yuvtab_2568[buf0[i+1]>>7];
			int U=((uvbuf0[i>>1]*uvalpha1+uvbuf1[i>>1]*uvalpha)>>19);
			int V=((uvbuf0[(i>>1)+2048]*uvalpha1+uvbuf1[(i>>1)+2048]*uvalpha)>>19);

			int Cb= yuvtab_40cf[U];
			int Cg= yuvtab_1a1e[V] + yuvtab_0c92[U];
			int Cr= yuvtab_3343[V];

			((uint16_t*)dest)[i] =
				clip_table15b[(Y1 + Cb) >>13] |
				clip_table15g[(Y1 + Cg) >>13] |
				clip_table15r[(Y1 + Cr) >>13];

			((uint16_t*)dest)[i+1] =
				clip_table15b[(Y2 + Cb) >>13] |
				clip_table15g[(Y2 + Cg) >>13] |
				clip_table15r[(Y2 + Cr) >>13];
		}
	}
#endif
}

// Bilinear / Bicubic scaling
static inline void RENAME(hScale)(int16_t *dst, int dstW, uint8_t *src, int srcW, int xInc,
				  int16_t *filter, int16_t *filterPos, int filterSize)
{
#ifdef HAVE_MMX
	if(filterSize==4) // allways true for upscaling, sometimes for down too
	{
		int counter= -2*dstW;
		filter-= counter*2;
		filterPos-= counter/2;
		dst-= counter/2;
		asm volatile(
			"pxor %%mm7, %%mm7		\n\t"
			"movq w02, %%mm6		\n\t"
			"pushl %%ebp			\n\t" // we use 7 regs here ...
			"movl %%eax, %%ebp		\n\t"
			".balign 16			\n\t"
			"1:				\n\t"
			"movzwl (%2, %%ebp), %%eax	\n\t"
			"movzwl 2(%2, %%ebp), %%ebx	\n\t"
			"movq (%1, %%ebp, 4), %%mm1	\n\t"
			"movq 8(%1, %%ebp, 4), %%mm3	\n\t"
			"movd (%3, %%eax), %%mm0	\n\t"
			"movd (%3, %%ebx), %%mm2	\n\t"
			"punpcklbw %%mm7, %%mm0		\n\t"
			"punpcklbw %%mm7, %%mm2		\n\t"
			"pmaddwd %%mm1, %%mm0		\n\t"
			"pmaddwd %%mm2, %%mm3		\n\t"
			"psrad $8, %%mm0		\n\t"
			"psrad $8, %%mm3		\n\t"
			"packssdw %%mm3, %%mm0		\n\t"
			"pmaddwd %%mm6, %%mm0		\n\t"
			"packssdw %%mm0, %%mm0		\n\t"
			"movd %%mm0, (%4, %%ebp)	\n\t"
			"addl $4, %%ebp			\n\t"
			" jnc 1b			\n\t"
			
			"popl %%ebp			\n\t"
			: "+a" (counter)
			: "c" (filter), "d" (filterPos), "S" (src), "D" (dst)
			: "%ebx"
		);
	}
	else if(filterSize==8)
	{
		int counter= -2*dstW;
		filter-= counter*4;
		filterPos-= counter/2;
		dst-= counter/2;
		asm volatile(
			"pxor %%mm7, %%mm7		\n\t"
			"movq w02, %%mm6		\n\t"
			"pushl %%ebp			\n\t" // we use 7 regs here ...
			"movl %%eax, %%ebp		\n\t"
			".balign 16			\n\t"
			"1:				\n\t"
			"movzwl (%2, %%ebp), %%eax	\n\t"
			"movzwl 2(%2, %%ebp), %%ebx	\n\t"
			"movq (%1, %%ebp, 8), %%mm1	\n\t"
			"movq 16(%1, %%ebp, 8), %%mm3	\n\t"
			"movd (%3, %%eax), %%mm0	\n\t"
			"movd (%3, %%ebx), %%mm2	\n\t"
			"punpcklbw %%mm7, %%mm0		\n\t"
			"punpcklbw %%mm7, %%mm2		\n\t"
			"pmaddwd %%mm1, %%mm0		\n\t"
			"pmaddwd %%mm2, %%mm3		\n\t"

			"movq 8(%1, %%ebp, 8), %%mm1	\n\t"
			"movq 24(%1, %%ebp, 8), %%mm5	\n\t"
			"movd 4(%3, %%eax), %%mm4	\n\t"
			"movd 4(%3, %%ebx), %%mm2	\n\t"
			"punpcklbw %%mm7, %%mm4		\n\t"
			"punpcklbw %%mm7, %%mm2		\n\t"
			"pmaddwd %%mm1, %%mm4		\n\t"
			"pmaddwd %%mm2, %%mm5		\n\t"
			"paddd %%mm4, %%mm0		\n\t"
			"paddd %%mm5, %%mm3		\n\t"
						
			"psrad $8, %%mm0		\n\t"
			"psrad $8, %%mm3		\n\t"
			"packssdw %%mm3, %%mm0		\n\t"
			"pmaddwd %%mm6, %%mm0		\n\t"
			"packssdw %%mm0, %%mm0		\n\t"
			"movd %%mm0, (%4, %%ebp)	\n\t"
			"addl $4, %%ebp			\n\t"
			" jnc 1b			\n\t"
			
			"popl %%ebp			\n\t"
			: "+a" (counter)
			: "c" (filter), "d" (filterPos), "S" (src), "D" (dst)
			: "%ebx"
		);
	}
	else
	{
		int counter= -2*dstW;
//		filter-= counter*filterSize/2;
		filterPos-= counter/2;
		dst-= counter/2;
		asm volatile(
			"pxor %%mm7, %%mm7		\n\t"
			"movq w02, %%mm6		\n\t"
			".balign 16			\n\t"
			"1:				\n\t"
			"movl %2, %%ecx			\n\t"
			"movzwl (%%ecx, %0), %%eax	\n\t"
			"movzwl 2(%%ecx, %0), %%ebx	\n\t"
			"movl %5, %%ecx			\n\t"
			"pxor %%mm4, %%mm4		\n\t"
			"pxor %%mm5, %%mm5		\n\t"
			"2:				\n\t"
			"movq (%1), %%mm1		\n\t"
			"movq (%1, %6), %%mm3		\n\t"
			"movd (%%ecx, %%eax), %%mm0	\n\t"
			"movd (%%ecx, %%ebx), %%mm2	\n\t"
			"punpcklbw %%mm7, %%mm0		\n\t"
			"punpcklbw %%mm7, %%mm2		\n\t"
			"pmaddwd %%mm1, %%mm0		\n\t"
			"pmaddwd %%mm2, %%mm3		\n\t"
			"paddd %%mm3, %%mm5		\n\t"
			"paddd %%mm0, %%mm4		\n\t"
			"addl $8, %1			\n\t"
			"addl $4, %%ecx			\n\t"
			"cmpl %4, %%ecx			\n\t"
			" jb 2b				\n\t"
			"addl %6, %1			\n\t"
			"psrad $8, %%mm4		\n\t"
			"psrad $8, %%mm5		\n\t"
			"packssdw %%mm5, %%mm4		\n\t"
			"pmaddwd %%mm6, %%mm4		\n\t"
			"packssdw %%mm4, %%mm4		\n\t"
			"movl %3, %%eax			\n\t"
			"movd %%mm4, (%%eax, %0)	\n\t"
			"addl $4, %0			\n\t"
			" jnc 1b			\n\t"
			
			: "+r" (counter)
			: "r" (filter), "m" (filterPos), "m" (dst), "m"(src+filterSize),
			  "m" (src), "r" (filterSize*2)
			: "%ebx", "%eax", "%ecx"
		);
	}
#else
	int i;
	for(i=0; i<dstW; i++)
	{
		int j;
		int srcPos= filterPos[i];
		int val=0;
//		printf("filterPos: %d\n", hFilterPos[i]);
		for(j=0; j<filterSize; j++)
		{
//			printf("filter: %d, src: %d\n", filter[i], src[srcPos + j]);
			val += ((int)src[srcPos + j])*filter[filterSize*i + j];
		}
//		filter += hFilterSize;
		dst[i] = MIN(MAX(0, val>>7), (1<<15)-1); // the cubic equation does overflow ...
//		dst[i] = val>>7;
	}
#endif
}
      // *** horizontal scale Y line to temp buffer
static inline void RENAME(hyscale)(uint16_t *dst, int dstWidth, uint8_t *src, int srcW, int xInc)
{
    if(sws_flags != SWS_FAST_BILINEAR)
    {
    	RENAME(hScale)(dst, dstWidth, src, srcW, xInc, hLumFilter, hLumFilterPos, hLumFilterSize);
    }
    else // Fast Bilinear upscale / crap downscale
    {
#ifdef ARCH_X86
#ifdef HAVE_MMX2
	int i;
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

#define FUNNY_Y_CODE \
			PREFETCH" 1024(%%esi)		\n\t"\
			PREFETCH" 1056(%%esi)		\n\t"\
			PREFETCH" 1088(%%esi)		\n\t"\
			"call funnyYCode		\n\t"\
			"movq temp0, %%mm2		\n\t"\
			"xorl %%ecx, %%ecx		\n\t"

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
		for(i=dstWidth-1; (i*xInc)>>16 >=srcW-1; i--) dst[i] = src[srcW-1]*128;
	}
	else
	{
#endif
	//NO MMX just normal asm ...
	asm volatile(
		"xorl %%eax, %%eax		\n\t" // i
		"xorl %%ebx, %%ebx		\n\t" // xx
		"xorl %%ecx, %%ecx		\n\t" // 2*xalpha
		".balign 16			\n\t"
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
	int i;
	unsigned int xpos=0;
	for(i=0;i<dstWidth;i++)
	{
		register unsigned int xx=xpos>>16;
		register unsigned int xalpha=(xpos&0xFFFF)>>9;
		dst[i]= (src[xx]<<7) + (src[xx+1] - src[xx])*xalpha;
		xpos+=xInc;
	}
#endif
    }
}

inline static void RENAME(hcscale)(uint16_t *dst, int dstWidth,
				uint8_t *src1, uint8_t *src2, int srcW, int xInc)
{
    if(sws_flags != SWS_FAST_BILINEAR)
    {
    	RENAME(hScale)(dst     , dstWidth, src1, srcW, xInc, hChrFilter, hChrFilterPos, hChrFilterSize);
    	RENAME(hScale)(dst+2048, dstWidth, src2, srcW, xInc, hChrFilter, hChrFilterPos, hChrFilterSize);
    }
    else // Fast Bilinear upscale / crap downscale
    {
#ifdef ARCH_X86
#ifdef HAVE_MMX2
	int i;
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

#define FUNNYUVCODE \
			PREFETCH" 1024(%%esi)		\n\t"\
			PREFETCH" 1056(%%esi)		\n\t"\
			PREFETCH" 1088(%%esi)		\n\t"\
			"call funnyUVCode		\n\t"\
			"movq temp0, %%mm2		\n\t"\
			"xorl %%ecx, %%ecx		\n\t"

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
		for(i=dstWidth-1; (i*xInc)>>16 >=srcW/2-1; i--)
		{
			dst[i] = src1[srcW/2-1]*128;
			dst[i+2048] = src2[srcW/2-1]*128;
		}
	}
	else
	{
#endif
	asm volatile(
		"xorl %%eax, %%eax		\n\t" // i
		"xorl %%ebx, %%ebx		\n\t" // xx
		"xorl %%ecx, %%ecx		\n\t" // 2*xalpha
		".balign 16			\n\t"
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
	int i;
	unsigned int xpos=0;
	for(i=0;i<dstWidth;i++)
	{
		register unsigned int xx=xpos>>16;
		register unsigned int xalpha=(xpos&0xFFFF)>>9;
		dst[i]=(src1[xx]*(xalpha^127)+src1[xx+1]*xalpha);
		dst[i+2048]=(src2[xx]*(xalpha^127)+src2[xx+1]*xalpha);
/* slower
	  dst[i]= (src1[xx]<<7) + (src1[xx+1] - src1[xx])*xalpha;
	  dst[i+2048]=(src2[xx]<<7) + (src2[xx+1] - src2[xx])*xalpha;
*/
		xpos+=xInc;
	}
#endif
   }
}

static void inline RENAME(initFilter)(int16_t *filter, int16_t *filterPos, int *filterSize, int xInc,
				      int srcW, int dstW)
{
	int i;
#ifdef HAVE_MMX
	asm volatile("emms\n\t"::: "memory"); //FIXME this shouldnt be required but it IS
#endif
	
	if(xInc <= (1<<16)) // upscale / cubic interpolate
	{
		int i;
		int xDstInSrc;
		if(sws_flags==SWS_BICUBIC) *filterSize= 4;
		else			   *filterSize= 2;
//		printf("%d %d %d\n", filterSize, srcW, dstW);
#ifdef HAVE_MMX
		*filterSize= (*filterSize +3) & (~3); // -> *filterSize %4 == 0
#endif
		xDstInSrc= xInc - 0x8000;
		for(i=0; i<dstW; i++)
		{
			int xx= (xDstInSrc>>16) - (*filterSize>>1) + 1;
			int j;
			
			filterPos[i]= xx;
			if(sws_flags == SWS_BICUBIC)
			{				
				double d= ABS(((xx+1)<<16) - xDstInSrc)/(double)(1<<16);
//				int coeff;
				int y1,y2,y3,y4;
				double A= -0.75;
					// Equation is from VirtualDub
		y1 = (int)floor(0.5 + (        +     A*d -       2.0*A*d*d +       A*d*d*d) * 16384.0);
		y2 = (int)floor(0.5 + (+ 1.0             -     (A+3.0)*d*d + (A+2.0)*d*d*d) * 16384.0);
		y3 = (int)floor(0.5 + (        -     A*d + (2.0*A+3.0)*d*d - (A+2.0)*d*d*d) * 16384.0);
		y4 = (int)floor(0.5 + (                  +           A*d*d -       A*d*d*d) * 16384.0);
						
//				printf("%d %d %d \n", coeff, (int)d, xDstInSrc);
				filter[i*(*filterSize) + 0]= y1;
				filter[i*(*filterSize) + 1]= y2;
				filter[i*(*filterSize) + 2]= y3;
				filter[i*(*filterSize) + 3]= y4;
//				printf("%1.3f %d, %d, %d, %d\n",d , y1, y2, y3, y4);
			}
			else 
			{
				for(j=0; j<*filterSize; j++)
				{
					double d= ABS((xx<<16) - xDstInSrc)/(double)(1<<16);
					int coeff;
					coeff= (int)(0.5 + (1.0 - d)*(1<<14));
					if(coeff<0) coeff=0;					
	//				printf("%d %d %d \n", coeff, (int)d, xDstInSrc);
					filter[i*(*filterSize) + j]= coeff;
					xx++;
				}
			}		
			xDstInSrc+= xInc;			
		}
	}
	else // downscale
	{
		int xDstInSrc;
		if(sws_flags==SWS_BICUBIC) *filterSize= (int)ceil(1 + 4.0*srcW / (double)dstW);
		else			   *filterSize= (int)ceil(1 + 2.0*srcW / (double)dstW);
//		printf("%d %d %d\n", *filterSize, srcW, dstW);
#ifdef HAVE_MMX
		*filterSize= (*filterSize +3) & (~3); // -> *filterSize %4 == 0
#endif
		xDstInSrc= xInc - 0x8000;
		for(i=0; i<dstW; i++)
		{
			int xx= (int)((double)xDstInSrc/(double)(1<<16) - *filterSize*0.5 + 0.5);
			int j;
			
			filterPos[i]= xx;
			for(j=0; j<*filterSize; j++)
			{
				double d= ABS((xx<<16) - xDstInSrc)/(double)xInc;
				int coeff;
				if(sws_flags == SWS_BICUBIC)
				{				
					double A= -0.75;
//					d*=2;
					// Equation is from VirtualDub
					if(d<1.0)
						coeff = (int)floor(0.5 + (1.0 - (A+3.0)*d*d 
						        + (A+2.0)*d*d*d) * (1<<14));
					else if(d<2.0)
						coeff = (int)floor(0.5 + (-4.0*A + 8.0*A*d 
						        - 5.0*A*d*d + A*d*d*d) * (1<<14));
					else
						coeff=0;
				}
				else 
				{
					coeff= (int)(0.5 + (1.0 - d)*(1<<14));
					if(coeff<0) coeff=0;					
				}
//				printf("%d %d %d \n", coeff, (int)d, xDstInSrc);
				filter[i*(*filterSize) + j]= coeff;
				xx++;
			}
			xDstInSrc+= xInc;
		}
	}
	
	//fix borders
	for(i=0; i<dstW; i++)
	{
		int j;
		if(filterPos[i] < 0)
		{
			// Move filter coeffs left to compensate for filterPos
			for(j=1; j<*filterSize; j++)
			{
				int left= MAX(j + filterPos[i], 0);
				filter[i*(*filterSize) + left] += filter[i*(*filterSize) + j];
				filter[i*(*filterSize) + j]=0;
			}
			filterPos[i]= 0;
		}

		if(filterPos[i] + *filterSize > srcW)
		{
			int shift= filterPos[i] + *filterSize - srcW;
			// Move filter coeffs right to compensate for filterPos
			for(j=*filterSize-2; j>=0; j--)
			{
				int right= MIN(j + shift, *filterSize-1);
				filter[i*(*filterSize) +right] += filter[i*(*filterSize) +j];
				filter[i*(*filterSize) +j]=0;
			}
			filterPos[i]= srcW - *filterSize; 
		}
	}
	
	//Normalize
	for(i=0; i<dstW; i++)
	{
		int j;
		double sum=0;
		double scale=1<<14;
		for(j=0; j<*filterSize; j++)
		{
			sum+= filter[i*(*filterSize) + j];
		}
		scale/= sum;
		for(j=0; j<*filterSize; j++)
		{
			filter[i*(*filterSize) + j]= (int)(filter[i*(*filterSize) + j]*scale);
		}
	}
}

static void RENAME(SwScale_YV12slice)(unsigned char* srcptr[],int stride[], int srcSliceY ,
			     int srcSliceH, uint8_t* dstptr[], int dststride, int dstbpp,
			     int srcW, int srcH, int dstW, int dstH){


unsigned int s_xinc2;
//FIXME do we need th +-2 stuff?
unsigned int s_xinc= (srcW << 16) / dstW - 2;
unsigned int s_yinc= (srcH << 16) / dstH + 2;
			  
static int lumDstYInSrc; // points to the dst Pixels center in the source (0 is the center of pixel 0,0 in src)
static int dstY;

// last horzontally interpolated lines, used to avoid unnecessary calculations
static int lastLumSrcY;
static int lastChrSrcY;

static int oldDstW= -1;
static int oldSrcW= -1;

int dstUVw;
int i;

if(((dstW + 7)&(~7)) >= dststride) dstW&= ~7;

dstUVw= fullUVIpol ? dstW : dstW/2;

//printf("%d %d %d %d\n", srcW, srcH, dstW, dstH);
//printf("%d %d %d %d\n", s_xinc, s_yinc, srcSliceY, srcSliceH);

#ifdef HAVE_MMX2
canMMX2BeUsed= (s_xinc <= 0x10000 && (dstW&31)==0 && (srcW&15)==0) ? 1 : 0;
#endif

// match pixel 0 of the src to pixel 0 of dst and match pixel n-2 of src to pixel n-2 of dst
// n-2 is the last chrominance sample available
// FIXME this is not perfect, but noone shuld notice the difference, the more correct variant
// would be like the vertical one, but that would require some special code for the
// first and last pixel
if(canMMX2BeUsed) 	s_xinc+= 20;
else			s_xinc = ((srcW-2)<<16)/(dstW-2) - 20;

if(fullUVIpol && !(dstbpp==12)) 	s_xinc2= s_xinc>>1;
else					s_xinc2= s_xinc;
  // force calculation of the horizontal interpolation of the first line

  if(srcSliceY ==0){
//	printf("dstW %d, srcw %d, mmx2 %d\n", dstW, srcW, canMMX2BeUsed);
	lastLumSrcY=-99;
	lastChrSrcY=-99;
	lumDstYInSrc= s_yinc/2 - 0x8000;
	dstY=0;

	// clean the buffers so that no green stuff is drawen if the width is not sane (%8=0)
	for(i=dstW-2; i<dstW+20; i++)
	{
		pix_buf_uv[0][i] = pix_buf_uv[1][i]
		= pix_buf_uv[0][2048+i] = pix_buf_uv[1][2048+i] = 128*128;
		pix_buf_uv[0][i/2] = pix_buf_uv[1][i/2]
		= pix_buf_uv[0][2048+i/2] = pix_buf_uv[1][2048+i/2] = 128*128;
		pix_buf_y[0][i]= pix_buf_y[1][i]= 0;
	}
	
	//precalculate horizontal scaler filter coefficients
    if(oldDstW!=dstW || oldSrcW!=srcW) 
    {
//	int i;
	oldDstW= dstW; oldSrcW= srcW;

	RENAME(initFilter)(hLumFilter, hLumFilterPos, &hLumFilterSize, s_xinc, srcW, dstW);
	RENAME(initFilter)(hChrFilter, hChrFilterPos, &hChrFilterSize, s_xinc2, srcW, dstW);
	
#ifdef HAVE_MMX2
// cant downscale !!!
	if(canMMX2BeUsed)
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

		for(i=0; i<dstW/8; i++)
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
   } // Init stuff
  } // reset counters

  while(1){
    unsigned char *dest =dstptr[0]+dststride*dstY;
    unsigned char *uDest=dstptr[1]+(dststride>>1)*(dstY>>1);
    unsigned char *vDest=dstptr[2]+(dststride>>1)*(dstY>>1);

    int lumSrcY=(lumDstYInSrc + 0xFFFF)>>16;  // first luminance source line number below the dst line
	// points to the dst Pixels center in the source (0 is the center of pixel 0,0 in src)
    int chrDstYInSrc= dstbpp==12 ?	lumDstYInSrc + s_yinc/2 - 0x8000 :
    					lumDstYInSrc            - 0x8000;
    int chrSrcY=(chrDstYInSrc + 0x1FFFF)>>17; // first chrominance source line number below the dst line
    int yalpha= ((lumDstYInSrc-1)&0xFFFF )>>4;
    int uvalpha=((chrDstYInSrc-1)&0x1FFFF)>>5;
    uint16_t *buf0=pix_buf_y[ lumSrcY   &1];	// top line of the interpolated slice
    uint16_t *buf1=pix_buf_y[(lumSrcY+1)&1];	// bottom line of the interpolated slice
    uint16_t *uvbuf0=pix_buf_uv[ chrSrcY   &1];	// top line of the interpolated slice
    uint16_t *uvbuf1=pix_buf_uv[(chrSrcY+1)&1];	// bottom line of the interpolated slice

    if(lumSrcY>=srcSliceY + srcSliceH && srcSliceY + srcSliceH < srcH) break;
    if(dstY >= dstH) break;
    
//	printf("lumSrcY:%d, dstY:%d, yalpha:%d\n", lumSrcY, dstY, yalpha*100/0x1000);

    if((dstY&1) && dstbpp==12) uvalpha=-1;

    dstY++; lumDstYInSrc+=s_yinc;

    //only interpolate the src line horizontally if we didnt do it allready
	if(lastLumSrcY!=lumSrcY)
	{
		unsigned char *src;
		
		// skip if first line has been horiz scaled alleady
		if(lastLumSrcY != lumSrcY-1)
		{
			// check if first line is before any available src lines
			if(lumSrcY-1 < srcSliceY ) src=srcptr[0]+(0                   )*stride[0];
			else			   src=srcptr[0]+(lumSrcY-srcSliceY -1)*stride[0];

			RENAME(hyscale)(buf0, dstW, src, srcW, s_xinc);
		}
		// check if second line is after any available src lines
		if(lumSrcY-srcSliceY  >= srcSliceH) src=srcptr[0]+(srcSliceH-1       )*stride[0];
		else				    src=srcptr[0]+(lumSrcY-srcSliceY )*stride[0];

		// the min() is required to avoid reuseing lines which where not available
		lastLumSrcY= MIN(lumSrcY, srcSliceY +srcSliceH-1);
		RENAME(hyscale)(buf1, dstW, src, srcW, s_xinc);
	}
//	printf("%d %d %d %d\n", y, chrSrcY, lastChrSrcY, h);
      // *** horizontal scale U and V lines to temp buffer
	if(lastChrSrcY!=chrSrcY)
	{
		uint8_t *src1, *src2;
		// skip if first line has been horiz scaled alleady
		if(lastChrSrcY != chrSrcY-1)
		{
			// check if first line is before any available src lines
			if(chrSrcY-srcSliceY /2-1 < 0)
			{
				src1= srcptr[1]+(0)*stride[1];
				src2= srcptr[2]+(0)*stride[2];
			}else{
				src1= srcptr[1]+(chrSrcY-srcSliceY /2-1)*stride[1];
				src2= srcptr[2]+(chrSrcY-srcSliceY /2-1)*stride[2];
			}
			RENAME(hcscale)(uvbuf0, dstUVw, src1, src2, srcW, s_xinc2);
		}

		// check if second line is after any available src lines
		if(chrSrcY - srcSliceY /2 >= srcSliceH/2)
		{
			src1= srcptr[1]+(srcSliceH/2-1)*stride[1];
			src2= srcptr[2]+(srcSliceH/2-1)*stride[2];
		}else{
			src1= srcptr[1]+(chrSrcY-srcSliceY /2)*stride[1];
			src2= srcptr[2]+(chrSrcY-srcSliceY /2)*stride[2];
		}
		RENAME(hcscale)(uvbuf1, dstUVw, src1, src2, srcW, s_xinc2);

		// the min() is required to avoid reuseing lines which where not available
		lastChrSrcY= MIN(chrSrcY, srcSliceY /2+srcSliceH/2-1);
	}
#ifdef HAVE_MMX
	b5Dither= dither8[dstY&1];
	g6Dither= dither4[dstY&1];
	g5Dither= dither8[dstY&1];
	r5Dither= dither8[(dstY+1)&1];
#endif

	if(dstbpp==12) //YV12
		RENAME(yuv2yuv)(buf0, buf1, uvbuf0, uvbuf1, dest, uDest, vDest, dstW, yalpha, uvalpha);
	else if(ABS(s_yinc - 0x10000) < 10)
		RENAME(yuv2rgb1)(buf0, buf1, uvbuf0, uvbuf1, dest, dstW, yalpha, uvalpha, dstbpp);
	else
		RENAME(yuv2rgbX)(buf0, buf1, uvbuf0, uvbuf1, dest, dstW, yalpha, uvalpha, dstbpp);
  }

#ifdef HAVE_MMX
	__asm __volatile(SFENCE:::"memory");
	__asm __volatile(EMMS:::"memory");
#endif
}
