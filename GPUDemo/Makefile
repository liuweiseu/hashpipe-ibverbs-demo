CC	= gcc

DEF		= 
TARGET  = gpu_demo

INC		= gpulib.h
SRC		= demo.c

FLAG  	= -I./GPULib/include \
          -L./GPULib \
		  -lgpu -lm

SUB_DIR = GPULib

${TARGET}: ${SRC} ${SUB_DIR}
	${CC} ${SRC} ${DEF} -o $@  ${FLAG} 

${SUB_DIR}: ECHO
	make -C $@

ECHO:
	@echo Going to compile .so in ${SUB_DIR}...

.PHONY: clean install
install:
	make -C ${SUB_DIR} install
clean:
	rm ${TARGET}
	make -C ${SUB_DIR} clean