/*
 *
 *  rgb2rgb.c, Software RGB to RGB convertor
 *  Written by Nick Kurshev.
 */
#include <inttypes.h>
#include "../config.h"
#include "rgb2rgb.h"
#include "../mmx_defs.h"

#ifdef HAVE_MMX
static const uint64_t mask32   __attribute__((aligned(8))) = 0x00FFFFFF00FFFFFFULL;
static const uint64_t mask24l  __attribute__((aligned(8))) = 0x0000000000FFFFFFULL;
static const uint64_t mask24h  __attribute__((aligned(8))) = 0x0000FFFFFF000000ULL;
static const uint64_t mask15b  __attribute__((aligned(8))) = 0x001F001F001F001FULL; /* 00000000 00011111  xxB */
static const uint64_t mask15rg __attribute__((aligned(8))) = 0x7FE07FE07FE07FE0ULL; /* 01111111 11100000  RGx */
static const uint64_t mask15s  __attribute__((aligned(8))) = 0xFFE0FFE0FFE0FFE0ULL;
#endif

void rgb24to32(const uint8_t *src,uint8_t *dst,uint32_t src_size)
{
  uint8_t *dest = dst;
  const uint8_t *s = src;
  const uint8_t *end;
#ifdef HAVE_MMX
  uint8_t *mm_end;
#endif
  end = s + src_size;
#ifdef HAVE_MMX
  __asm __volatile(PREFETCH"	%0"::"m"(*s):"memory");
  mm_end = (uint8_t*)((((unsigned long)end)/(MMREG_SIZE*2))*(MMREG_SIZE*2));
  __asm __volatile("movq	%0, %%mm7"::"m"(mask32):"memory");
  if(mm_end == end) mm_end -= MMREG_SIZE*2;
  while(s < mm_end)
  {
    __asm __volatile(
	PREFETCH"	32%1\n\t"
	"movd	%1, %%mm0\n\t"
	"movd	3%1, %%mm1\n\t"
	"movd	6%1, %%mm2\n\t"
	"movd	9%1, %%mm3\n\t"
	"punpckldq %%mm1, %%mm0\n\t"
	"punpckldq %%mm3, %%mm2\n\t"
	"pand	%%mm7, %%mm0\n\t"
	"pand	%%mm7, %%mm2\n\t"
	MOVNTQ"	%%mm0, %0\n\t"
	MOVNTQ"	%%mm2, 8%0"
	:"=m"(*dest)
	:"m"(*s)
	:"memory");
    dest += 16;
    s += 12;
  }
  __asm __volatile(SFENCE:::"memory");
  __asm __volatile(EMMS:::"memory");
#endif
  while(s < end)
  {
    *dest++ = *s++;
    *dest++ = *s++;
    *dest++ = *s++;
    *dest++ = 0;
  }
}

void rgb32to24(const uint8_t *src,uint8_t *dst,uint32_t src_size)
{
  uint8_t *dest = dst;
  const uint8_t *s = src;
  const uint8_t *end;
#ifdef HAVE_MMX
  uint8_t *mm_end;
#endif
  end = s + src_size;
#ifdef HAVE_MMX
  __asm __volatile(PREFETCH"	%0"::"m"(*s):"memory");
  mm_end = (uint8_t*)((((unsigned long)end)/(MMREG_SIZE*2))*(MMREG_SIZE*2));
  __asm __volatile(
	"movq	%0, %%mm7\n\t"
	"movq	%1, %%mm6"
	::"m"(mask24l),"m"(mask24h):"memory");
  if(mm_end == end) mm_end -= MMREG_SIZE*2;
  while(s < mm_end)
  {
    __asm __volatile(
	PREFETCH"	32%1\n\t"
	"movq	%1, %%mm0\n\t"
	"movq	8%1, %%mm1\n\t"
	"movq	%%mm0, %%mm2\n\t"
	"movq	%%mm1, %%mm3\n\t"
	"psrlq	$8, %%mm2\n\t"
	"psrlq	$8, %%mm3\n\t"
	"pand	%%mm7, %%mm0\n\t"
	"pand	%%mm7, %%mm1\n\t"
	"pand	%%mm6, %%mm2\n\t"
	"pand	%%mm6, %%mm3\n\t"
	"por	%%mm2, %%mm0\n\t"
	"por	%%mm3, %%mm1\n\t"
	MOVNTQ"	%%mm0, %0\n\t"
	MOVNTQ"	%%mm1, 6%0"
	:"=m"(*dest)
	:"m"(*s)
	:"memory");
    dest += 12;
    s += 16;
  }
  __asm __volatile(SFENCE:::"memory");
  __asm __volatile(EMMS:::"memory");
#endif
  while(s < end)
  {
    *dest++ = *s++;
    *dest++ = *s++;
    *dest++ = *s++;
    s++;
  }
}

/*
 Original by Strepto/Astral
 ported to gcc & bugfixed : A'rpi
 MMX2, 3DNOW optimization by Nick Kurshev
 32bit c version, and and&add trick by Michael Niedermayer
*/
void rgb15to16(const uint8_t *src,uint8_t *dst,uint32_t src_size)
{
#ifdef HAVE_MMX
  register const char* s=src+src_size;
  register char* d=dst+src_size;
  register int offs=-src_size;
  __asm __volatile(PREFETCH"	%0"::"m"(*(s+offs)));
  __asm __volatile(
	"movq	%0, %%mm4\n\t"
	::"m"(mask15s));
  while(offs<0)
  {
	__asm __volatile(
		PREFETCH"	32%1\n\t"
		"movq	%1, %%mm0\n\t"
		"movq	8%1, %%mm2\n\t"
		"movq	%%mm0, %%mm1\n\t"
		"movq	%%mm2, %%mm3\n\t"
		"pand	%%mm4, %%mm0\n\t"
		"pand	%%mm4, %%mm2\n\t"
		"paddw	%%mm1, %%mm0\n\t"
		"paddw	%%mm3, %%mm2\n\t"
		MOVNTQ"	%%mm0, %0\n\t"
		MOVNTQ"	%%mm2, 8%0"
		:"=m"(*(d+offs))
		:"m"(*(s+offs))
		);
	offs+=16;
  }
  __asm __volatile(SFENCE:::"memory");
  __asm __volatile(EMMS:::"memory");
#else
#if 0
   const uint16_t *s1=( uint16_t * )src;
   uint16_t *d1=( uint16_t * )dst;
   uint16_t *e=((uint8_t *)s1)+src_size;
   while( s1<e ){
     register int x=*( s1++ );
     /* rrrrrggggggbbbbb
        0rrrrrgggggbbbbb
        0111 1111 1110 0000=0x7FE0
        00000000000001 1111=0x001F */
     *( d1++ )=( x&0x001F )|( ( x&0x7FE0 )<<1 );
   }
#else
	const uint32_t *s1=( uint32_t * )src;
	uint32_t *d1=( uint32_t * )dst;
	int i;
	int size= src_size>>2;
	for(i=0; i<size; i++)
	{
		register int x= s1[i];
//		d1[i] = x + (x&0x7FE07FE0); //faster but need msbit =0 which might not allways be true
		d1[i] = (x&0x7FFF7FFF) + (x&0x7FE07FE0);

	}
#endif
#endif
}

/**
 * Pallete is assumed to contain bgr32
 */
void palette8torgb32(uint8_t *src, uint8_t *dst, int src_size, uint8_t *palette)
{
	int i;
	for(i=0; i<src_size; i++)
		((uint32_t *)dst)[i] = ((uint32_t *)palette)[ src[i] ];
}

/**
 * Pallete is assumed to contain bgr32
 */
void palette8torgb24(uint8_t *src, uint8_t *dst, int src_size, uint8_t *palette)
{
	int i;
/*
	writes 1 byte o much and might cause alignment issues on some architectures?
	for(i=0; i<src_size; i++)
		((uint32_t *)(&dst[i*3])) = ((uint32_t *)palette)[ src[i] ];
*/
	for(i=0; i<src_size; i++)
	{
		//FIXME slow?
		dst[0]= palette[ src[i]*4+0 ];
		dst[1]= palette[ src[i]*4+1 ];
		dst[2]= palette[ src[i]*4+2 ];
		dst+= 3;
	}
}

void rgb32to16(uint8_t *src, uint8_t *dst, int src_size)
{
	int i;
	for(i=0; i<src_size; i+=4)
	{
		const int b= src[i+0];
		const int g= src[i+1];
		const int r= src[i+2];

		((uint16_t *)dst)[i]= (b>>3) | ((g&0xFC)<<3) | ((r&0xF8)<<8);
	}
}

void rgb32to15(uint8_t *src, uint8_t *dst, int src_size)
{
	int i;
	for(i=0; i<src_size; i+=4)
	{
		const int b= src[i+0];
		const int g= src[i+1];
		const int r= src[i+2];

		((uint16_t *)dst)[i]= (b>>3) | ((g&0xF8)<<3) | ((r&0xF8)<<7);
	}
}


/**
 * Palette is assumed to contain bgr16, see rgb32to16 to convert the palette
 */
void palette8torgb16(uint8_t *src, uint8_t *dst, int src_size, uint8_t *palette)
{
	int i;
	for(i=0; i<src_size; i++)
		((uint16_t *)dst)[i] = ((uint16_t *)palette)[ src[i] ];
}

/**
 * Pallete is assumed to contain bgr15, see rgb32to15 to convert the palette
 */
void palette8torgb15(uint8_t *src, uint8_t *dst, int src_size, uint8_t *palette)
{
	int i;
	for(i=0; i<src_size; i++)
		((uint16_t *)dst)[i] = ((uint16_t *)palette)[ src[i] ];
}

void yv12toyuy2(uint8_t *ysrc, uint8_t *usrc, uint8_t *vsrc, uint8_t *dst, int src_size)
{
	int i;
	src_size>>=1;
	for(i=0; i<src_size; i++)
	{
		dst[4*i+0] = ysrc[2*i+0];
		dst[4*i+1] = usrc[i];
		dst[4*i+2] = ysrc[2*i+1];
		dst[4*i+3] = vsrc[i];
	}

}

void yuy2toyv12(uint8_t *src, uint8_t *ydst, uint8_t *udst, uint8_t *vdst, int src_size)
{
	int i;
	src_size>>=1;
	for(i=0; i<src_size; i++)
	{
		 ydst[2*i+0] 	= src[4*i+0];
		 udst[i] 	= src[4*i+1];
		 ydst[2*i+1] 	= src[4*i+2];
		 vdst[i] 	= src[4*i+3];
	}
}