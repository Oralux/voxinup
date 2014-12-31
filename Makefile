ttsynth_CFLAGS = \
	-ggdb \
	-I/opt/IBM/ibmtts/inc

ttsynth_LIBS = \
	-lasound \
	-libmeci

all: spk-connect-ttsynth

spk-connect-ttsynth: ttsynth.c
	gcc $(ttsynth_CFLAGS) $(ttsynth_LIBS) ttsynth.c -o spk-connect-ttsynth

clean:
	rm spk-connect-ttsynth
