DEBUGFLAGS = -g -W -Wall
BUILDFLAGS = $(DEBUGFLAGS) -D_REENTRANT
CC = gcc

all: raft

raft: raft.o
	$(CC) -g -o raft raft.o -lssh

raft.o: raft.c
	$(CC) $(BUILDFLAGS) -c -o raft.o raft.c

clean:
	rm -f *.o
	rm -f raft
