CC=gcc
CFLAGS=-g -Wall

all: tests

lib_tar.o: lib_tar.c lib_tar.h
	$(CC) $(CFLAGS) -c lib_tar.c -o lib_tar.o

tests: tests.c lib_tar.o
	#tar --posix --pax-option delete=".*" --pax-option delete="*time*" --no-xattrs --no-acl --no-selinux -c testing.txt empty.txt alpha.txt > tester.tar
	$(CC) $(CFLAGS) -o tests tests.c lib_tar.o 

clean:
	rm -f lib_tar.o tests soumission.tar

submit: all
	tar --posix --pax-option delete=".*" --pax-option delete="*time*" --no-xattrs --no-acl --no-selinux -c *.h *.c Makefile > soumission.tar
