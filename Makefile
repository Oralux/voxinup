PREFIX = /usr
BINDIR = ${PREFIX}/bin

SRCS = ttsynth.c player.c debug.c
SCRIPTS = jupiter-spk-run.sh  jupiter-spk-stop.sh
OBJS = ${SRCS:.c=.o}
LDLIBS = -lasound -libmeci
CFLAGS = -I/opt/IBM/ibmtts/inc -Wall 
CFLAGS += -ggdb -DDEBUG
LDFLAGS = 

INSTALL=install

all: voxinup

install: voxinup
	${INSTALL} -d ${DESTDIR}/${BINDIR}
	${INSTALL} -m 0755 $< ${SCRIPTS} ${DESTDIR}/${BINDIR}

voxinup: ${OBJS}
	${CC} ${LDFLAGS} -o $@ $^ ${LDLIBS}

clean:
	${RM} ${OBJS} voxinup
