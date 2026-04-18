.phony all:
all: ACS

ACS: ACS.c
	gcc -Wall -pthread ACS.c -o ACS

.PHONY clean:
clean:
	-rm -rf *.o *.exe
