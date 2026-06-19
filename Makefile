CC = clang
CFLAGS = -Wall -Wextra -O2 -std=c99
TARGET = bloomfilter
SOURCES = csrc/bloomfilter_raw.c
OBJECTS = $(SOURCES:.c=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(CFLAGS) -o $@ $^ -lm

%.o: %.c hash.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJECTS)
	rm results.csv
