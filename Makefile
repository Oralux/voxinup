PREFIX = /usr
BINDIR = ${PREFIX}/bin

SRCS = ttsynth.c
SCRIPTS = jupiter-spk-run.sh  jupiter-spk-stop.sh
OBJS = ${SRCS:.c=.o}
LDLIBS = -lasound -libmeci
CFLAGS = -I/opt/IBM/ibmtts/inc -Wall 
#CFLAGS += -ggdb
LDFLAGS = 

INSTALL=install

all: spk-connect-ttsynth

install: spk-connect-ttsynth
	${INSTALL} -d ${DESTDIR}/${BINDIR}
	${INSTALL} -m 0755 $< ${SCRIPTS} ${DESTDIR}/${BINDIR}

spk-connect-ttsynth: ${OBJS}
	${CC} ${LDFLAGS} -o $@ $^ ${LDLIBS}

clean:
	${RM} ${OBJS} spk-connect-ttsynth
