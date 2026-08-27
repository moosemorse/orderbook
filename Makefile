BUILD_DIR := build

.PHONY: all release clean bench

all:
	cmake -S . -B $(BUILD_DIR)
	cmake --build $(BUILD_DIR) -j

release:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR) -j

# perf numbers are only meaningful from a release build - always rebuilds release first
bench: release
	./$(BUILD_DIR)/orderbook_bench $(ARGS)

clean:
	rm -rf $(BUILD_DIR)
