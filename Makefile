all: woof

woof: woof.c
	gcc -Wall -O2 -o woof woof.c `pkg-config --cflags --libs glib-2.0` `pkg-config --cflags --libs gmime-2.6`

clean:
	rm *.o woof
