################################################################################
#                     ____    _    _   _ _   _ _____ _____                     #
#                    / ___|  / \  | \ | | | | |_   _| ____|                    #
#                    | |    / _ \ |  \| | | | | | | |  _|                      #
#                    | |___/ ___ \| |\  | |_| | | | | |___                     #
#                    \____/_/   \_\_| \_|\___/  |_| |_____|                    #
#                                                                              #
#                                   MAKEFILE                                   #
#                                                                              #
################################################################################

CC      ?= /usr/bin/gcc
CCROSS  ?= /usr/bin/i586-mingw32msvc-gcc
CFLAGS  := -O2 -Wall -pipe -fomit-frame-pointer
LDFLAGS := -Wl,-s,-O1

Header      := canute.h
Sources     := canute.c feedback.c file.c net.c protocol.c
Objects     := $(Sources:.c=.o)
HaseObjects := $(Sources:.c=.obj)

# Phony targets
.PHONY: unix hase debug clean help

# Target aliases
unix : canute
hase : canute.exe
debug: canute.dbg

# Binaries
canute: $(Objects)
	@echo ' Linking           $@' && $(CC) $(LDFLAGS) -o $@ $^

canute.exe: $(HaseObjects)
	@echo ' Linking   [win32] $@' && \
	$(CCROSS) -L/usr/i586-mingw32msvc/lib $(LDFLAGS) -o $@ $^ -lwsock32

canute.dbg: $(Sources) $(Header)
	@echo ' Building  [debug] $@' && \
	$(CC) -Wall -pipe -O0 -g -pg -DDEBUG -o $@ $(filter %.c, $^)

# Pattern rules
%.o: %.c $(Header)
ifdef ARCH
	@echo ' Compiling [tuned] $@' && $(CC) $(CFLAGS) $(ARCH) -c $<
else
	@echo ' Compiling         $@' && $(CC) $(CFLAGS) -c $<
endif

%.obj: %.c $(Header)
	@echo ' Compiling [win32] $@' && $(CCROSS) $(CFLAGS) -c -o $@ $< 

%.s: %.c $(Header)
	@echo ' Assembling        $@' && $(CC) $(CFLAGS) $(ARCH) -S $<

# Cleaning and help
clean:
	@-rm -fv $(Objects) $(HaseObjects) canute canute.exe canute.dbg

help:
	@echo 'User targets:'
	@echo ''
	@echo '	unix  - Default target. Build the UNIX binary.'
	@echo '	hase  - Build the Hasefroch binary (win32).'
	@echo '	debug - Build the UNIX binary with debugging support.'
	@echo '	clean - Clean objects and binaries.'
	@echo '	help  - This help.'
	@echo ''
	@echo 'NOTE: Enable custom optimization flags for the UNIX binary'
	@echo '      defining the ARCH make variable. For example:'
	@echo ''
	@echo '      make "ARCH=-march=pentium-m -mfpmath=sse" unix'
	@echo ''

