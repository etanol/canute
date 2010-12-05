################################################################################
#                 ____      _      _   _   _   _   _____   _____               #
#                / ___|    / \    | \ | | | | | | |_   _| | ____|              #
#               | |       / _ \   |  \| | | | | |   | |   |  _|                #
#               | |___   / ___ \  | |\  | | |_| |   | |   | |___               #
#                \____| /_/   \_\ |_| \_|  \___/    |_|   |_____|              #
#                                                                              #
#                                   MAKEFILE                                   #
#                                                                              #
################################################################################

UNAME := $(shell uname)

CC       := gcc
HCC      := i586-mingw32msvc-gcc
HCC64    := amd64-mingw32msvc-gcc
CFLAGS   := -pipe -O3 -Wall -fomit-frame-pointer
LDFLAGS  := -Wl,-s
DBGFLAGS := -pipe -Wall -O0 -g -pg -DDEBUG

ifeq ($(UNAME),SunOS)
	CC       := cc
	CFLAGS   := -DOMIT_HERROR -xO3
	LDFLAGS  := -s
	DBGFLAGS := -DOMIT_HERROR -DDEBUG -xO0 -g
	LIBS     := -lsocket -lnsl
endif

ifeq ($(UNAME),HP-UX)
	CC       := cc
	CFLAGS   := -D_XOPEN_SOURCE_EXTENDED +O3 #+DAportable
	LDFLAGS  := -s
	DBGFLAGS := -DDEBUG +O0 -g
endif

# OSF1 support didn't make it to 1.2.  Dropped until resources available
ifeq ($(UNAME),OSF1)
	CC       := cc
	CFLAGS   :=
	LDFLAGS  :=
	DBGFLAGS := -DDEBUG -g
endif

Header        := canute.h
Sources       := canute.c feedback.c net.c protocol.c util.c
Objects       := $(Sources:.c=.o)
HaseObjects   := $(Sources:.c=.obj)
HaseObjects64 := $(Sources:.c=.obj64)

# Phony targets
.PHONY: unix hase hase64 debug clean help dist

# Target aliases
unix  : canute
hase  : canute.exe
hase64: canute64.exe
debug : canute.dbg

# Binaries
canute: $(Objects)
	@echo ' Linking           $@' && $(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

canute.exe: $(HaseObjects)
	@echo ' Linking   [win32] $@' && \
	$(HCC) $(LDFLAGS) -o $@ $^ -lwsock32

canute64.exe: $(HaseObjects64)
	@echo ' Linking   [win64] $@' && \
	$(HCC64) $(LDFLAGS) -o $@ $^ -lws2_32

canute.dbg: $(Sources) $(Header)
	@echo ' Building  [debug] $@' && \
	$(CC) $(DBGFLAGS) -o $@ $(filter %.c, $^) $(LIBS)

# Pattern rules
%.o: %.c $(Header)
ifdef ARCH
	@echo ' Compiling [tuned] $@' && $(CC) $(CFLAGS) $(ARCH) -c $<
else
	@echo ' Compiling         $@' && $(CC) $(CFLAGS) -c $<
endif

%.obj: %.c $(Header)
	@echo ' Compiling [win32] $@' && $(HCC) $(CFLAGS) -c -o $@ $<

%.obj64: %.c $(Header)
	@echo ' Compiling [win64] $@' && $(HCC64) $(CFLAGS) -c -o $@ $<

%.s: %.c $(Header)
	@echo ' Assembling        $@' && $(CC) $(CFLAGS) $(ARCH) -S $<

# Repository maintainer targets (not listed in help)
dist:
	@v=`hg id -t | head -1`                                            ; \
	if [ -z "$$v" ] || [ "$$v" = 'tip' ]                               ; \
	then                                                                 \
	        t=`hg tags | sed -n -e '2 s/ .*$$// p'`                    ; \
	        rb=`hg id -nr $$t`                                         ; \
	        rc=`hg id -n | sed -e 's/[^0-9][^0-9]*//g'`                ; \
	        v="$$t+`expr $$rc - $$rb`"                                 ; \
	fi                                                                 ; \
	hg archive -t tgz -X .hg_archival.txt -X .hgtags canute-$$v.tar.gz

# Cleaning and help
clean:
	@-echo ' Cleaning objects and binaries' && \
	rm -f $(Objects) $(HaseObjects) $(HaseObjects64) canute canute.exe canute64.exe canute.dbg

help:
	@echo 'User targets:'
	@echo ''
	@echo '	unix   - Default target. Build the UNIX binary.'
	@echo '	hase   - Build the Hasefroch binary (win32).'
	@echo '	hase64 - Build the Hasefroch binary (win64).'
	@echo '	debug  - Build the UNIX binary with debugging support.'
	@echo '	clean  - Clean objects and binaries.'
	@echo '	help   - This help.'
	@echo ''
	@echo 'NOTE: Enable custom optimization flags for the UNIX binary'
	@echo '      defining the ARCH make variable. For example:'
	@echo ''
	@echo '	$(MAKE) "ARCH=-march=pentium-m -mfpmath=sse" unix'
	@echo ''

