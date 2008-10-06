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
CFLAGS   := -O3 -Wall -fomit-frame-pointer
LDFLAGS  := -Wl,-s
DBGFLAGS := -Wall -O0 -g -pg -DDEBUG

ifeq ($(UNAME),SunOS)
	CC       := cc
	CFLAGS   := -DOMIT_HERROR -xO3
	LDFLAGS  := -s
	DBGFLAGS := -DOMIT_HERROR -DDEBUG -xO0 -g
	LIBS     := -lsocket -lnsl
endif

Header      := canute.h
Sources     := canute.c feedback.c net.c protocol.c util.c
Objects     := $(Sources:.c=.o)
HaseObjects := $(Sources:.c=.obj)

# Phony targets
.PHONY: unix hase debug clean help dist

# Target aliases
unix : canute
hase : canute.exe
debug: canute.dbg

# Binaries
canute: $(Objects)
	@echo ' Linking           $@' && $(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

canute.exe: $(HaseObjects)
	@echo ' Linking   [win32] $@' && \
	$(HCC) -L/usr/i586-mingw32msvc/lib $(LDFLAGS) -o $@ $^ -lwsock32

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

%.s: %.c $(Header)
	@echo ' Assembling        $@' && $(CC) $(CFLAGS) $(ARCH) -S $<

# Repository maintainer targets (not listed in help)
dist:
	@v=`hg id -t | head -1` ; \
	{ test -z "$$v" || test "$$v" = 'tip' ; } && v=`hg id -i | head -1` ; \
	hg archive -t tgz -X .hg_archival.txt -X .hgtags canute-$$v.tar.gz

# Cleaning and help
clean:
	@-echo ' Cleaning objects and binaries' && \
	rm -f $(Objects) $(HaseObjects) canute canute.exe canute.dbg

help:
	@echo 'User targets:'
	@echo ''
	@echo '	unix  - Default target. Build the UNIX binary.'
	@echo '	hase  - Build the Hasefroch binary (win32).'
	@echo '	debug - Build the UNIX binary with debugging support.'
	@echo '	clean  - Clean objects and binaries.'
	@echo '	help  - This help.'
	@echo ''
	@echo 'NOTE: Enable custom optimization flags for the UNIX binary'
	@echo '      defining the ARCH make variable. For example:'
	@echo ''
	@echo '      make "ARCH=-march=pentium-m -mfpmath=sse" unix'
	@echo ''

