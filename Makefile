CC = gcc
CFLAGS = -O3 -fopenmp
LIBS = -lopenblas -lm

all: Ergasia0

Ergasia0: Ergasia0.c
	$(CC) $(CFLAGS) Ergasia0.c -o Ergasia0 $(LIBS)

clean:
	rm -f Ergasia0
