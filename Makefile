VERSION_MAJOR = 0
VERSION_MINOR = 2

VERSION = $(VERSION_MAJOR).$(VERSION_MINOR)
CC = g++
CFLAGS = -Ofast -DVERSION_MAJOR=$(VERSION_MAJOR) -DVERSION_MINOR=$(VERSION_MINOR)
EXECNAME = flash-bench

MODULE_OBJECTS = flash-bench.o cpu-stat.o

$(EXECNAME) : $(MODULE_OBJECTS)
	$(CC) $(CFLAGS) $(MODULE_OBJECTS) -o $(EXECNAME) -lpthread -lm

.cpp.o :
	$(CC) -c $(CFLAGS) $< -o $@

clean :
	rm -f $(MODULE_OBJECTS) $(EXECNAME) .depend

dep :
	rm -f .depend
	make .depend

.depend: Makefile
	rm -f .depend
	echo '# Module dependencies' >> .depend
	g++ -MM $(patsubst %.o,%.cpp,$(MODULE_OBJECTS)) >> .depend

include .depend
