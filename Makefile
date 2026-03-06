CC = gcc
CFLAGS = -D_DEFAULT_SOURCE -Wall -Wextra -std=c11 -g

TARGET = ecu

SRCS = ecu.c pgn_data.c stack_utils.c
OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
