CC          = gcc
LIB_CCFLAGS     = -g -O3 -fPIC -shared -msse4


LIB_TARGET   = hp_ibv_demo.so

LIB_INCLUDES = -I./include \
				-I/usr/local/include
LIB_LINKS	 = -L. -L/usr/local/lib \
			   -lstdc++ -lhashpipe -lhashpipe_ibverbs -lrt -lm

LIB_DIR		 = lib
SCRIPT_DIR	 = scripts

LIB_SRCS = ${wildcard ./src/*.c}

all: $(LIB_TARGET)

$(LIB_TARGET): files ${LIB_SRCS} 
	$(CC) ${LIB_SRCS} -o $@ ${LIB_INCLUDES} ${LIB_LINKS} ${LIB_CCFLAGS} 
	@mv ${LIB_TARGET} ${LIB_DIR}
files :
	@echo ${LIB_SRCS}	

tags:
	ctags -R .
	
clean:
	rm -f ${LIB_DIR}/${LIB_TARGET} tags

prefix=/usr/local
LIBDIR=${prefix}/lib
BINDIR=${prefix}/bin
install-lib: ${LIB_DIR}/${LIB_TARGET}
	mkdir -p "${LIBDIR}"
	install -p $^ "${LIBDIR}"

install-scripts: ${SCRIPT_DIR}/*.sh
	mkdir -p "${BINDIR}"
	install -p $^ "${BINDIR}"

install: install-lib install-scripts

.PHONY: all tags clean install install-lib