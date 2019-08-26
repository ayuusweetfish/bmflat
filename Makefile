all: flattest flatspin
flattest: bmflat.o flattest.o
flatspin: bmflat.o flatspin.o stb_vorbis.o tinyfiledialogs.o
	$(LD) $(LDFLAGS) -o $@ $^ \
		-lm -ldl -lpthread -lGL -lGLEW -lglfw

bmflat.o: bmflat.h bmflat.c
flattest.o: bmflat.h flattest.c
flatspin.o: bmflat.h flatspin.c
stb_vorbis.o: miniaudio/extras/stb_vorbis.c
	$(CC) $(CFLAGS) -c -o stb_vorbis.o miniaudio/extras/stb_vorbis.c
tinyfiledialogs.o: tinyfiledialogs/tinyfiledialogs.c
	$(CC) $(CFLAGS) -c -o tinyfiledialogs.o tinyfiledialogs/tinyfiledialogs.c

.PHONY: clean
clean:
	$(RM) flattest flatspin \
	bmflat.o flattest.o flatspin.o flatspin.o stb_vorbis.o tinyfiledialogs.o
