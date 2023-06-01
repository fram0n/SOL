CC		=  gcc
CFLAGS	        += -Wall -Werror -g -std=c99
INCDIR		= ./includes
INCLUDES	= -I. -I $(INCDIR)
LDFLAGS 	= -L.
LIBS            = -lpthread -lm
TEST		= test.sh

# aggiungere qui altri targets
TARGETS		= farm generafile



.PHONY: all clean cleanall
.SUFFIXES: .c .h

all		: $(TARGETS)

farm: farm.c MasterThread.c threadpool.c Worker.c Collector.c
	$(CC) $(CFLAGS) $(INCLUDES) $(LDFLAGS) -o $@ $^ $(LIBS)

generafile: generafile.c
	$(CC) $(CFLAGS) -o $@ $^

test: farm generafile
	chmod +x $(TEST)
	./$(TEST)

clean		: 
	-rm -f $(TARGETS)

cleanall	: clean
	-rm -f *.o *~ *.a *.dat
	-rm -r testdir
	-rm expected.txt
	-rm -f farm.sck



