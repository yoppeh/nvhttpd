#
# Makefile
#
# Targets:
#   all
#       The default target, if no target is specified. Compiles source files
#       as necessary and links them into the final executable.
#   bear
#       Generates a compile_commands.json file for use with LSPs.
#   clean
#       Removes all object files and executables.
#   install
#       Installs executable and scripts into the directory specified by the prefix
#       variable. Defaults to /usr/local.
#   uninstall
#       Removes the executable from prefix/bin, conf file from /etc, systemd service 
#       files from /etc/systemd and /usr/lib/systemd
# Variables:
#   CC
#       The C compiler to use. Defaults to gcc.
#   CFLAGS
#       Flags to pass to the C compiler. Defaults to -I/usr/include.
#   debug=1
#       Build with debug logging to stdout
#	gdb=1
#		Build with debugging symbols
#   LDFLAGS
#       Flags to pass to the linker. Defaults to -L/usr/lib.
#   prefix
#       The directory to install into. Defaults to /usr/local. Executables will be
#       installed into $(prefix)/bin, nvhttpd.conf in /etc, nvhttpd.service in 
#       /usr/lib/systemd/system (which is then linked to 
#       /etc/systemd/system/multi-user.target.wants)
#

ifeq ($(CC),)
	CC = gcc
endif
CFLAGS += -I/usr/include
LDFLAGS += -L/usr/lib
ifdef debug
	CFLAGS += -D DEBUG
endif
ifdef gdb
	CFLAGS += -g3
else
	CFLAGS += -O3 -flto
	LDFLAGS += -flto
endif
ifeq ($(prefix),)
	prefix = /usr/local
endif

EXES = nvhttpd
OBJS = main.o cache.o config.o debug.o http.o log.o request.o response.o
LIBS = -lssl -lcrypto

.PHONY: all bear clean help install uninstall

all: $(EXES)

bear:
	make clean
	bear -- make

clean:
	- rm -f $(EXES)
	- rm -f *.o

nvhttpd: $(OBJS)
	$(CC) $(LDFLAGS) $(LIBS) $^ -o $@
ifndef gdb
	strip $@
endif

cache.o: cache.c cache.h debug.h
config.o: config.c config.h debug.h
debug.o: debug.c debug.h
http.o: http.c debug.h http.h log.h
log.o: log.c log.h
main.o: main.c cache.h debug.h http.h log.h request.h response.h
request.o: request.c debug.h http.h log.h request.h
response.o: response.c debug.h http.h log.h request.h response.h

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

help:
	@echo -e "Targets:"
	@echo -e "  all"
	@echo -e "    The default target, if no target is specified. Compiles source files"
	@echo -e "    as necessary and links them into the final executable."
	@echo -e "  bear"
	@echo -e "    Generates a compile_commands.json file for use with LSPs."
	@echo -e "  clean"
	@echo -e "    Removes all object files and executables."
	@echo -e "  install"
	@echo -e "    Installs executable and scripts into the directory specified by the prefix"
	@echo -e "    variable. Defaults to /usr/local."
	@echo -e "  uninstall"
	@echo -e "    Removes the executable from prefix/bin, conf file from /etc, systemd service"
	@echo -e "    files from /etc/systemd and /usr/lib/systemd. Removes the executable from bin,"
	@echo -e "    all the scripts from sbin."
	@echo -e "Variables:"
	@echo -e "  CC"
	@echo -e "    The C compiler to use. Defaults to gcc."
	@echo -e "  CFLAGS"
	@echo -e "    Flags to pass to the C compiler. Defaults to -I/usr/include."
	@echo -e "  debug=1"
	@echo -e "    Build with debug logging to stdout"
	@echo -e "  gdb=1"
	@echo -e "    Build with debugging symbols"
	@echo -e "  LDFLAGS"
	@echo -e "    Flags to pass to the linker. Defaults to -L/usr/lib."
	@echo -e "  prefix"
	@echo -e "    The directory to install into. Defaults to /usr/local. Executables will be"
	@echo -e "    installed into $(prefix)/bin, nvhttpd.conf in /etc, nvhttpd.service in"
	@echo -e "    /usr/lib/systemd/system (which is then linked to"
	@echo -e "    /etc/systemd/system/multi-user.target.wants)"

install: $(EXES)
	install -d $(prefix)/bin
	install -d /etc/nvhttpd
	install -m 755 $(EXES) $(prefix)/bin
	install -m 755 nvhttpd.conf /etc/nvhttpd
	cat nvhttpd.service | sed s:##PREFIX##:$(prefix):g > /usr/lib/systemd/system/nvhttpd.service
	ln -s /usr/lib/systemd/system/nvhttpd.service /etc/systemd/multi-user.target.wants/nvhttpd.service

uninstall:
	- rm -f /etc/systemd/system/multi-user.target.wants/nvhttpd.service
	- rm -f /usr/lib/systemd/system/nvhttpd.service
	- rm -rf /etc/nvhttpd
	- rm -f $(prefix)/bin/$(EXES)
