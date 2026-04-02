CXX      := g++
CXXFLAGS := -std=c++20 -Wall -Wextra -Wpedantic -Wshadow -Wconversion
 
TARGET   := orderbook
SRC      := main.cpp OrderBook.cpp
INC      := -I.
 
# Debug build (default)
.PHONY: all
all: CXXFLAGS += -g -O0
all: $(TARGET)
 
# Release build
.PHONY: release
release: CXXFLAGS += -O2 -DNDEBUG
release: $(TARGET)
 
$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $(INC) -o $@ $^
 
.PHONY: clean
clean:
	rm -f $(TARGET)
 