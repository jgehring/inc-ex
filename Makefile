#
# Inc-Ex - Report redundant includes in C/C++ code
#

LLVM_PREFIX   = /usr
CFLAGS       += -g -Wall -ansi -pedantic -I$(LLVM_PREFIX)/include
DEFINES      += -D_BSD_SOURCE
LDFLAGS      += -L$(LLVM_PREFIX)/lib/llvm
LIBS         +=  -lclang

PROGRAM = incex
SOURCES = main.c

OBJECTS = $(SOURCES:.c=.o)

all: $(PROGRAM)

.c.o:
	$(CC) $(CFLAGS) $(DEFINES) -c -o $@ $<

$(PROGRAM): $(OBJECTS)
	$(CC) $(LDFLAGS) $(LIBS) $(OBJECTS) -o $@

clean:
	rm -rf $(PROGRAM) $(OBJECTS)


# Testing
TESTS = $(wildcard tests/*.c.in)

tests/*.c.in: FORCE
	@./$(PROGRAM) $@ -x c 

check: $(PROGRAM) $(TESTS)

FORCE:
