CC = gcc 

SRC = ibv_sender.c

LIB = -libverbs

TARGET = sender


${TARGET}: ${SRC}
	${CC} ${SRC} -o ${TARGET} ${LIB}

clean:
	rm ${TARGET}
