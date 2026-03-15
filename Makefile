CC = cc
CFLAGS = -Os -g

.PHONY: all check clean clobber

all: test_bracha87 consensus

bracha87.o: bracha87.c bracha87.h
	$(CC) $(CFLAGS) -c -o $@ bracha87.c

test_bracha87: test/test_bracha87.c bracha87.o bracha87.h
	$(CC) $(CFLAGS) -I. -o $@ test/test_bracha87.c bracha87.o

consensus: example/consensus.c bracha87.o bracha87.h
	$(CC) $(CFLAGS) -I. -o $@ example/consensus.c bracha87.o

check: test_bracha87
	./test_bracha87

clean:
	rm -f bracha87.o test_bracha87 consensus

clobber: clean
	rm -rf test/*.dSYM example/*.dSYM
