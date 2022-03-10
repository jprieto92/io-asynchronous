BIN = select epoll

CC = gcc
CFLAGS = -Wall -g -o
LIBS = -lpthread

all:
	$(CC) $(CFLAGS) select select.c $(LIBS)
	$(CC) $(CFLAGS) epoll epoll.c $(LIBS)
clean:
	rm -f *.o $(BIN)
