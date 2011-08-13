#
# Inc-Ex - Report redundant includes in C/C++ code
#

# $(CLANG_INCLUDE)/clang-c/Index.h is needed
CLANG_INCLUDE = -I/usr/include
# Link against libclang
CLANG_LIBS    = -L/usr/lib/llvm -lclang


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
TESTS = $(wildcard tests/*.c.in) $(wildcard tests/*.cpp.in)

tests/*.c.in: FORCE
	@./$(PROGRAM) -x c $@

tests/*.cpp.in: FORCE
	@./$(PROGRAM) -x c++ $@

check: $(PROGRAM) $(TESTS)

FORCE:
