CC       = gcc
CFLAGS   = -D_DEFAULT_SOURCE -Wall -Wextra -std=c11 -g
LDFLAGS  = -lpthread
TARGET   = ecu
BUILDDIR = build

# Main application
SRCS     = $(wildcard src/*.c)
OBJS     = $(SRCS:src/%.c=$(BUILDDIR)/%.o)
DEPS     = $(OBJS:.o=.d)

# Tools
TOOL_SRCS = $(wildcard tools/*.c)
TOOLS     = $(TOOL_SRCS:tools/%.c=$(BUILDDIR)/%)

.PHONY: all clean

all: $(BUILDDIR)/$(TARGET) $(TOOLS)

# Link main application
$(BUILDDIR)/$(TARGET): $(OBJS) | $(BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LDFLAGS)

# Compile main application sources
$(BUILDDIR)/%.o: src/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -MMD -MP -Isrc -c $< -o $@

# Compile and link each tool (single file, no -lpthread needed)
$(BUILDDIR)/%: tools/%.c | $(BUILDDIR)
	$(CC) $(CFLAGS) -Isrc -o $@ $<

$(BUILDDIR):
	mkdir -p $(BUILDDIR)

-include $(DEPS)

clean:
	rm -rf $(BUILDDIR)
