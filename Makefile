# Makefile for index

CC=gcc
CFLAGS=-O -Wall -g -m32
OBJS=index.o strmatch.o table.o csv.o html.o form.o creole.o
all: index.cgi

index.cgi: $(OBJS)
	$(CC) -o index.cgi $(OBJS)

install: index.cgi
	cp index.cgi $$HOME/public_html/atvid-tk.org/cgi-bin

install-test: index.cgi
	cp index.cgi $$HOME/public_html/atvid-tk.org/cgi-bin/index-test.cgi

clean:
	-rm -f index.cgi core *.o *~ \#*
