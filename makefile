PLUGINS = lowpass highpass bandpass bandreject delayreverb
CFLAGS = -Wall -fPIC -DPIC -O3
LDFLAGS = -shared -lm

default: all

all: $(PLUGINS)

$(PLUGINS):
	gcc $(CFLAGS) -c $@.c -o $@.o
	gcc $(LDFLAGS) -o $@.so $@.o
	rm -f $@.o
	mv $@.so bin/

clean:
	rm -f *.o *.so

.PHONY: all clean default $(PLUGINS)
