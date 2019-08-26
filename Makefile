all: flattest flatspin
flattest: bmflat.o flattest.o
	$(LD) $(LDFLAGS) -o $@ $^
flatspin: bmflat.o flatspin.o stb_vorbis.o tinyfiledialogs.o
	$(LD) $(LDFLAGS) -o $@ $^ \
		-lm -ldl -lpthread -lGL -lGLEW -lglfw

bmflat.o: bmflat.c bmflat.h
	$(CC) $(CFLAGS) -c -o $@ $<
flattest.o: flattest.c bmflat.h
	$(CC) $(CFLAGS) -c -o $@ $<
flatspin.o: flatspin.c bmflat.h
	$(CC) $(CFLAGS) -c -o $@ $<
stb_vorbis.o: miniaudio/extras/stb_vorbis.c
	$(CC) $(CFLAGS) -c -o $@ $<
tinyfiledialogs.o: tinyfiledialogs/tinyfiledialogs.c
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: clean
clean:
	$(RM) flattest flatspin \
	bmflat.o flattest.o flatspin.o flatspin.o stb_vorbis.o tinyfiledialogs.o
