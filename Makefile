GCC=gcc
FLAGS=-g -Wall -Werror
INCLUDE_HEADERS_DIRECTORY=-Iheaders

all: tests lib_tar.o

lib_tar.o: lib_tar.c lib_tar.h

tests: tests.c lib_tar.o
	tar --posix --pax-option delete=".*" --pax-option delete="*time*" --no-xattrs --no-acl --no-selinux -c testing.txt > tester.tar
	gcc tests.c -o tests.o
	./tests.o tester.tar

tests: tests.c lib_tar.o
	$(GCC) $(INCLUDE_HEADERS_DIRECTORY) $(CFLAGS) -o $@ tests.c lib_tar.c


clean:
	rm -f lib_tar.o tests soumission.tar

submit: all
	tar --posix --pax-option delete=".*" --pax-option delete="*time*" --no-xattrs --no-acl --no-selinux -c *.h *.c Makefile > soumission.tar
