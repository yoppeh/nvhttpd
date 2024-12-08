#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles source files
#       as necessary and links them into the final executable.
#   bear
#      Generates a compile_commands.json file for use with LSPs.
#   clean
#       Removes all object files and executables.
#   install
#      Installs executable and scripts into the directory specified by the prefi    x
#      variable. Defaults to /usr/local.
#   uninstall
#      Removes the executabl from bin, all the scripts from sbin
# Variables:
#   CC
#      The C compiler to use. Defaults to gcc.
#   CFLAGS
#      Flags to pass to the C compiler. Defaults to -I/usr/include. If libcurl
#      or libjson-c are installed in a non-standard location, you may need to
#      add -I/path/to/include to this variable. On Mac OS, for example, I use
#      MacPorts, so I use make CFLAGS="-I/opt/local/include".
#   debug=1
#       Build with debug info
#   LDFLAGS
#      Flags to pass to the linker. Defaults to -L/usr/lib. If libcurl or
#      libjson-c are installed in a non-standard location, you may need to add
#      -L/path/to/lib to this variable. On Mac OS, for example, I use MacPorts,
#      so I use make LDFLAGS="-L/opt/local/lib".
#   prefix
#      The directory to install into. Defaults to /usr/local. Executables will b    e
#      installed into $(prefix)/bin, scripts in $(prefix)/sbin
#

ifeq ($(CC),)
	CC = gcc
endif
CFLAGS += -I/usr/include
LDFLAGS += -L/usr/lib
ifdef debug
	CFLAGS += -g3 -D DEBUG
else
	CFLAGS += -O3 -flto
	LDFLAGS += -flto
endif
ifeq ($(prefix),)
	prefix = /usr/local
endif

EXES = nvhttpd
OBJS = main.o cache.o config.o debug.o http.o logger.o request.o response.o

.PHONY: all bear clean install uninstall

all: $(EXES)

bear:
	make clean
	bear -- make

clean:
	- rm -f $(EXES)
	- rm -f *.o

nvhttpd: $(OBJS)
	$(CC) $(LDFLAGS) $(LIBS) $^ -o $@
ifndef debug
	strip $@
endif

cache.o: cache.c cache.h debug.h
config.o: config.c config.h debug.h
debug.o: debug.c debug.h
http.o: http.c debug.h http.h logger.h
logger.o: logger.c logger.h
main.o: main.c cache.h debug.h http.h logger.h request.h response.h
request.o: request.c debug.h http.h logger.h request.h
response.o: response.c debug.h http.h logger.h request.h response.h

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

install: $(EXES)
	install -d $(prefix)/bin
	install -m 755 $(EXES) $(prefix)/bin

uninstall:
	- rm -f $(prefix)/bin/$(EXES)