PREFIX = /usr
BINDIR = ${PREFIX}/bin

SRCS = ttsynth.c player.c debug.c
SRCS_TESTS = player.c debug.c
SCRIPTS = jupiter-spk-run.sh  jupiter-spk-stop.sh
OBJS = ${SRCS:.c=.o}
OBJS_TESTS = ${SRCS_TESTS:.c=.o}
LDLIBS = -lasound -ldl
CFLAGS = -I/opt/IBM/ibmtts/inc -Wall 
#CFLAGS += -ggdb -DDEBUG
LDFLAGS = 

INSTALL=install

all: voxinup

install: voxinup
	${INSTALL} -d ${DESTDIR}/${BINDIR}
	${INSTALL} -m 0755 $< ${SCRIPTS} ${DESTDIR}/${BINDIR}

uninstall:
	${RM} -f ${DESTDIR}/${BINDIR}/${SCRIPTS} ${DESTDIR}/${BINDIR}/voxinup

voxinup: ${OBJS}
	${CC} ${LDFLAGS} -o $@ $^ ${LDLIBS}

test1: test1.o ${OBJS_TESTS}
	${CC} ${LDFLAGS} -o $@ $^ ${LDLIBS}

test2: test2.o ${OBJS_TESTS}
	${CC} ${LDFLAGS} -o $@ $^ ${LDLIBS}

test.all: test1 test2
	./run.sh
clean:
	${RM} *.o voxinup test1 test2

