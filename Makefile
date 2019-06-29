#
# Makefile for fal2muc
#
# Copyright (c) 2019 Hirokuni Yano
#
# Released under the MIT license.
# see https://opensource.org/licenses/MIT
#
CC	= gcc
CFLAGS	= -Wall -Wextra

all: fal2muc txt2bas

clean:
	rm -f fal2muc fal2muc.o
	rm -f txt2bas txt2bas.o

fal2muc.o: fal2muc.c

fal2muc: fal2muc.o
	$(CC) fal2muc.o -o fal2muc

txt2bas.o: txt2bas.c

txt2bas: txt2bas.o
	$(CC) txt2bas.o -o txt2bas
