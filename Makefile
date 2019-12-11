CC=gcc
CFLAGS=-shared -Wall -Werror -fPIC -Iinclude -ldl -fvisibility=hidden
SRCS=$(wildcard src/*.c)
OUTPUTFILE=gshelper.so
OUTPUTPATH=build

all: bin

debug: CFLAGS += -DDEBUG -g
debug: bin

bin:
	mkdir -p ${OUTPUTPATH}
	${CC} ${SRCS} ${CFLAGS} -o ${OUTPUTPATH}/${OUTPUTFILE}

clean:
	rm -rf ${OUTPUT_PATH}
