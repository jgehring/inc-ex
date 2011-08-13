#
# Inc-Ex - Report redundant includes in C/C++ code
#

# LLVM / Clang location
LLVM_PREFIX   = /usr
CLANG_INCLUDE = -I$(LLVM_PREFIX)/include
CLANG_LIBS    = -L$(LLVM_PREFIX)/lib/llvm -lclang


CFLAGS       += -g -Wall -ansi -pedantic $(CLANG_INCLUDE)
DEFINES      += -D_BSD_SOURCE
LIBS         += $(CLANG_LIBS)

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
	@./$(PROGRAM) -x c $@

check: $(PROGRAM) $(TESTS)

FORCE:
