BUILD_DIR := build

.PHONY: all release clean

all:
	cmake -S . -B $(BUILD_DIR)
	cmake --build $(BUILD_DIR) -j

release:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=Release
	cmake --build $(BUILD_DIR) -j

clean:
	rm -rf $(BUILD_DIR)
