
include ../config.mak

SWSLIB = libswscale.a

SWSSRCS=swscale.c rgb2rgb.c yuv2rgb.c
SWSOBJS=$(SWSSRCS:.c=.o)

CFLAGS  = $(OPTFLAGS) $(MLIB_INC) -I. -I.. $(EXTRA_INC)
# -I/usr/X11R6/include/

.SUFFIXES: .c .o

# .PHONY: all clean

.c.o:
	$(CC) -c $(CFLAGS) -I.. -o $@ $<

all:    $(SWSLIB)

$(SWSLIB):     $(SWSOBJS)
	$(AR) r $(SWSLIB) $(SWSOBJS)
	$(RANLIB) $(SWSLIB)

clean:
	rm -f *.o *.a *~ *.so cs_test swscale-example

distclean:
	rm -f Makefile.bak *.o *.a *~ *.so .depend cs_test swscale-example

dep:    depend

depend:
	$(CC) -MM $(CFLAGS) $(SWSSRCS) 1>.depend

cs_test: cs_test.o $(SWSLIB)
	$(CC) cs_test.o $(SWSLIB) ../cpudetect.o -DFOR_MENCODER ../mp_msg.c -o cs_test -W -Wall

swscale-example: swscale-example.o $(SWSLIB)
	$(CC) swscale-example.o $(SWSLIB) ../libmpcodecs/img_format.o -lm -o swscale-example -W -Wall
#
# include dependency files if they exist
#
ifneq ($(wildcard .depend),)
include .depend
endif
