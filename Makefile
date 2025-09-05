#!/usr/bin/make -f
.PHONY: all clean

TARGET      = dumpcc
SRCS        = clear_code.c
OBJS        = $(SRCS:.c=.o)

CFLAGS      = --std=gnu99 -g -O0 -funsigned-char -Wall -Wextra -Wshadow
LDFLAGS     = -lm

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $^ -o $@ $(LDFLAGS)

clean:
	-$(RM) $(OBJS)
	-$(RM) $(TARGET)
