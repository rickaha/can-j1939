CC      = gcc
CFLAGS  = -D_DEFAULT_SOURCE -Wall -Wextra -std=c11 -g
LDFLAGS = -lpthread
TARGET  = ecu
BUILDDIR = build

SRCS    = main.c ecu.c pgn_data.c stack_utils.c sensors.c
OBJS    = $(SRCS:%.c=$(BUILDDIR)/%.o)
DEPS    = $(OBJS:.o=.d)

.PHONY: all clean

all: $(BUILDDIR)/$(TARGET)

$(BUILDDIR)/$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

$(BUILDDIR)/%.o: %.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

-include $(DEPS)

clean:
	rm -rf $(BUILDDIR)
