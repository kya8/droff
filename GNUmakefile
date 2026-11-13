CC ?= cc
CFLAGS ?= -O2 -g

sources := src/main.c src/utils.c src/device.c src/busy.c src/power.c
objects := $(sources:src/%.c=build/%.o)
dependencies := $(objects:.o=.d)

.PHONY: all clean
.DELETE_ON_ERROR:

all: build/droff

static: build/droff-static

build/droff: $(objects) GNUmakefile
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(objects) $(LDLIBS)

build/droff-static: $(objects) GNUmakefile
	$(CC) -static $(CFLAGS) $(LDFLAGS) -o $@ $(objects) $(LDLIBS)

build/%.o: src/%.c GNUmakefile | build
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=c11 -Wall -Wextra -Wpedantic -MMD -MP -c -o $@ $<

build:
	mkdir -p $@

clean:
	$(RM) $(objects) $(dependencies) build/droff build/droff-static

-include $(dependencies)
