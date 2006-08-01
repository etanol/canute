#
# Makefile para canute.c - Por C2H5OH
#
# Use 'make' para compilar tanto la versión POSIX como win32
#

CC      ?= /usr/bin/gcc
CCROSS  ?= /usr/bin/i586-mingw32msvc-gcc
CFLAGS  := -O2 -Wall -pipe -fomit-frame-pointer
LDFLAGS := -Wl,-s,-O1
ARCH    := -march=pentium-m -msse -mfpmath=sse

all: canute canute.exe

install: $(HOME)/bin/canute

$(HOME)/bin/canute: canute
	@cp -fv $< $@

canute: canute.o
	$(CC) $(LDFLAGS) -o $@ $^

canute.dbg: canute.c
	$(CC) -O0 -g -pg -o $@ $^

canute.exe: canute.obj
	$(CCROSS) -L/usr/i586-mingw32msvc/lib $(LDFLAGS) -o $@ $^ -lwsock32

%.o: %.c
	$(CC) $(CFLAGS) $(ARCH) -c $^

%.obj: %.c
	$(CCROSS) $(CFLAGS) -DHASEFROCH -c -o $@ $^ 

.PHONY: clean ChangeLog
clean:
	@-rm -fv *.o *.obj canute canute.exe

