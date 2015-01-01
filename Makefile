PREFIX = /usr
BINDIR = ${PREFIX}/bin

SRCS = ttsynth.c
OBJS = $(SRCS:.c=.o)
LDLIBS = -lasound -libmeci
CFLAGS = -m32 -I/opt/IBM/ibmtts/inc -Wall 
#CFLAGS += -ggdb
LDFLAGS = -m32

INSTALL=install

all: spk-connect-ttsynth

install: spk-connect-ttsynth
	$(INSTALL) -d $(DESTDIR)/$(BINDIR)
	$(INSTALL) -m 0755 $< $(DESTDIR)/$(BINDIR)

spk-connect-ttsynth: ${OBJS}
	${CC} ${LDFLAGS} -o $@ $^ $(LDLIBS)

clean:
	${RM} ${OBJS} spk-connect-ttsynth

#${SRCS:.c=.o}:	${SRCS}
