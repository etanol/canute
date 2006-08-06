#
# Makefile for canute - By C2H5OH
#
# Use 'make' to compile POSIX version and Hasefroch version 
#

CC      ?= /usr/bin/gcc
CCROSS  ?= /usr/bin/i586-mingw32msvc-gcc
CFLAGS  := -O2 -Wall -pipe -fomit-frame-pointer
LDFLAGS := -Wl,-s,-O1
#ARCH    := -march=pentium-m -msse -mfpmath=sse

Header      := canute.h
Sources     := $(wildcard *.c)
Objects     := $(Sources:.c=.o)
HaseObjects := $(Sources:.c=.obj)

all: canute canute.exe

install: $(HOME)/bin/canute

$(HOME)/bin/canute: canute
	@cp -fv $< $@

canute: $(Objects)
	@echo "LD         $@" && $(CC) $(LDFLAGS) -o $@ $^

canute.dbg: $(Sources)
	@echo "LD [debug] $@" && $(CC) -O0 -g -pg -o $@ $^

canute.exe: $(HaseObjects)
	@echo "LD [win32] $@" && \
	$(CCROSS) -L/usr/i586-mingw32msvc/lib $(LDFLAGS) -o $@ $^ -lwsock32

%.o: %.c $(Header)
	@echo "CC         $@" && $(CC) $(CFLAGS) $(ARCH) -c $<

%.obj: %.c $(Header)
	@echo "CC [win32] $@" && $(CCROSS) $(CFLAGS) -c -o $@ $< 

%.s: %.c $(Header)
	@echo "AS         $@" && $(CC) $(CFLAGS) $(ARCH) -S $<

.PHONY: clean ChangeLog
clean:
	@-rm -fv *.o *.obj canute canute.exe canute.dbg

