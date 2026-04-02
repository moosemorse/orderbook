CXX      := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -Wshadow -Wconversion
 
BUILDDIR := build
TARGET   := $(BUILDDIR)/orderbook
SRC      := src/*
INC      := -I. -Iinclude
 
# Debug build (default)
.PHONY: all
all: CXXFLAGS += -g -O0
all: $(TARGET)
 
# Release build
.PHONY: release
release: CXXFLAGS += -O2 -DNDEBUG
release: $(TARGET)
 
$(BUILDDIR):
	mkdir -p $(BUILDDIR)

$(TARGET): $(SRC) | $(BUILDDIR)
	$(CXX) $(CXXFLAGS) $(INC) -o $@ $^
 
.PHONY: clean
clean:
	rm -f $(TARGET)
 