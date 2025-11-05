# Makefile for OSAL (OS Abstraction Layer)

# Detect platform
UNAME_S := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    PLATFORM := linux
endif
ifeq ($(UNAME_S),Darwin)
    PLATFORM := macos
endif

# Compiler and flags
CC := gcc
CFLAGS := -Wall -Wextra -std=c99 -fPIC -I./include -D__LWS_PTHREAD__
LDFLAGS := -lpthread

# Directories
SRC_DIR := src/$(PLATFORM)
BUILD_DIR := build/$(PLATFORM)
LIB_DIR := lib

# Source files
SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(patsubst $(SRC_DIR)/%.c,$(BUILD_DIR)/%.o,$(SRCS))

# Output library
TARGET := $(LIB_DIR)/liblwsosal.a

# Default target
all: $(TARGET)

# Create directories
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

$(LIB_DIR):
	mkdir -p $(LIB_DIR)

# Compile source files
$(BUILD_DIR)/%.o: $(SRC_DIR)/%.c | $(BUILD_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Create static library
$(TARGET): $(OBJS) | $(LIB_DIR)
	ar rcs $@ $(OBJS)
	@echo "Built $(TARGET) for platform: $(PLATFORM)"

# Clean
clean:
	rm -rf build lib

# Install (copy to system location)
install: $(TARGET)
	@echo "Installing to /usr/local..."
	sudo mkdir -p /usr/local/include/lwsip
	sudo cp -r include/* /usr/local/include/lwsip/
	sudo cp $(TARGET) /usr/local/lib/
	@echo "Installation complete"

# Uninstall
uninstall:
	@echo "Uninstalling from /usr/local..."
	sudo rm -rf /usr/local/include/lwsip
	sudo rm -f /usr/local/lib/liblwsosal.a
	@echo "Uninstall complete"

# Help
help:
	@echo "OSAL Makefile"
	@echo "Platform detected: $(PLATFORM)"
	@echo ""
	@echo "Available targets:"
	@echo "  all      - Build OSAL library (default)"
	@echo "  clean    - Remove build artifacts"
	@echo "  install  - Install library and headers to /usr/local"
	@echo "  uninstall- Remove installed files"
	@echo "  help     - Show this help message"

.PHONY: all clean install uninstall help
