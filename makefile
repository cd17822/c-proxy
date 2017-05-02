.c.o:
	gcc -std=c11 -g -c $?

# compile client and server
all: multi-threaded-server 

# compile server program
multi-threaded-server: multi-threaded-server.o confutils.o
	gcc -std=c11 -g -o multi-threaded-server confutils.o multi-threaded-server.o -lpthread

clean:
	rm *.o
