NVCC	= nvcc

TARGET  = libgpu.so

INC 	= include/gpulib.h
SRC		= src/gpulib.cu

NVCCFLAG  = -Xcompiler -fPIC --shared \
			-I/usr/local/cuda/include -I./include\
		  	-L/usr/local/cuda/lib64	\
			-lcuda -arch=sm_86

${TARGET}: ${SRC} ${INC} 
	${NVCC} -o $@ ${SRC} ${NVCCFLAG}


.PHONY: clean install
install:
	install -p ${TARGET} "/usr/local/lib" 
clean:
	rm ${TARGET} 
