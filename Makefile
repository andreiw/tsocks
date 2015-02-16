# Generated automatically from Makefile.in by configure.
# Makefile used by configure to create real Makefile

CC=gcc
prefix=/usr
exec_prefix = ${prefix}
libexecdir = ${exec_prefix}/libexec
sysconfdir = ${prefix}/etc
libdir = /lib
infodir = ${prefix}/info
mandir = ${prefix}/man
includedir = ${prefix}/include

SHCC = ${CC} -fPIC 
INSPECT=inspectsocks
SAVE=saveme
LIB_NAME=libtsocks
COMMON=common
PARSER=parser
VALIDATECONF=validateconf
SHLIB_MAJOR=1
SHLIB_MINOR=7
SHLIB=${LIB_NAME}.so.${SHLIB_MAJOR}.${SHLIB_MINOR}

#CFLAGS=-O2 -Wall
INSTALL = /usr/bin/install -c
INSTALL_DATA = ${INSTALL} -m 644
CFLAGS = -g -O2
INCLUDES = -I.
LIBS = 
SPECIALLIBS = -ldl 

SHOBJS = ${OBJS:.o=.so}

OBJS= tsocks.o

TARGETS= ${SHLIB} ${UTIL_LIB} ${SAVE} ${INSPECT} ${VALIDATECONF}

all: ${TARGETS}

${COMMON}.o: ${COMMON}.c
	${SHCC} ${CFFLAGS} ${INCLUDES} -c -o ${COMMON}.o ${COMMON}.c

${PARSER}.o: ${PARSER}.c
	${SHCC} ${CFFLAGS} ${INCLUDES} -c -o ${PARSER}.o ${PARSER}.c

${VALIDATECONF}: ${VALIDATECONF}.c ${COMMON}.o ${PARSER}.o
	${SHCC} ${CFFLAGS} ${LIBS} ${INCLUDES} -o ${VALIDATECONF} ${VALIDATECONF}.c ${COMMON}.o ${PARSER}.o

${INSPECT}: ${INSPECT}.c ${COMMON}.o
	${SHCC} ${CFFLAGS} ${LIBS} ${INCLUDES} -o ${INSPECT} ${INSPECT}.c ${COMMON}.o

${SAVE}: ${SAVE}.c
	${SHCC} ${CFFLAGS} ${INCLUDES} -static -o ${SAVE} ${SAVE}.c

${SHLIB}: ${SHOBJS} ${COMMON}.o ${PARSER}.o
	${SHCC} ${CFLAGS} ${SPECIALLIBS} ${LIBS} ${INCLUDES} -nostdlib -shared -o ${SHLIB} ${SHOBJS} ${COMMON}.o ${PARSER}.o ${DYNLIB_FLAGS}
	ln -sf ${SHLIB} ${LIB_NAME}.so

%.so: %.c
	${SHCC} ${CFLAGS} ${INCLUDES} -c ${CC_SWITCHES} $< -o $@

%.o: %.c
	${SHCC} ${CFLAGS} ${INCLUDES} -c ${CC_SWITCHES} $< -o $@

install: ${TARGETS} installlib installman

installlib:
	${INSTALL} ${STATICLIB} ${SHLIB} ${libdir}
	ln -sf ${SHLIB} ${libdir}/${LIB_NAME}.so.${SHLIB_MAJOR}
	ln -sf ${LIB_NAME}.so.${SHLIB_MAJOR} ${libdir}/${LIB_NAME}.so

installman:
	if [ ! -d "${mandir}/man8" ] ; then \
		${INSTALL} -o root -g root -d ${mandir}/man8; \
	fi;
	${INSTALL_DATA} -o root -g root tsocks.8 ${mandir}/man8/
	if [ ! -d "${mandir}/man5" ] ; then \
		${INSTALL} -o root -g root -d ${mandir}/man5; \
	fi;
	${INSTALL_DATA} -o root -g root tsocks.conf.5 ${mandir}/man5/
	
clean:
	-rm -f *.so *.so.* *.o *~ ${TARGETS}

distclean: clean
	-rm -f config.cache config.log
