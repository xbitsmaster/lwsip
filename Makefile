# Makefile for lwsip project
# This Makefile helps build media-server dependencies and the main project

.PHONY: all deps clean clean-deps build-sdk build-avcodec build-media-server build-osal help

# Default target - only build required dependencies
all: deps-required lwsip

# Build only required dependencies (avcodec is optional)
deps-required: build-osal build-sdk build-media-server

# Build all dependencies including optional ones
deps: build-osal build-sdk build-avcodec build-media-server

help:
	@echo "Available targets:"
	@echo "  all              - Build required dependencies and lwsip (default)"
	@echo "  deps             - Build all dependencies including optional avcodec"
	@echo "  deps-required    - Build only required dependencies (osal + sdk + media-server)"
	@echo "  build-osal       - Build OSAL library"
	@echo "  build-sdk        - Build SDK library"
	@echo "  build-avcodec    - Build avcodec library (optional)"
	@echo "  build-media-server - Build media-server libraries"
	@echo "  lwsip            - Build lwsip using CMake"
	@echo "  clean            - Clean lwsip build artifacts"
	@echo "  clean-deps       - Clean all dependency build artifacts"
	@echo "  clean-all        - Clean everything"

# Build OSAL
build-osal:
	@echo "Building OSAL..."
	$(MAKE) -C osal

# Build SDK
build-sdk:
	@echo "Building SDK..."
	$(MAKE) -C 3rds/sdk

# Build avcodec
build-avcodec: build-sdk
	@echo "Building avcodec..."
	$(MAKE) -C 3rds/avcodec

# Build media-server
build-media-server: build-sdk
	@echo "Building media-server..."
	$(MAKE) -C 3rds/media-server

# Build all dependencies
deps: build-sdk build-avcodec build-media-server

# Build lwsip using CMake
lwsip:
	@echo "Building lwsip..."
	@./build.sh

# Clean lwsip build
clean:
	@./clean.sh

# Clean dependency builds
clean-deps:
	@echo "Cleaning dependencies..."
	-$(MAKE) -C osal clean
	-$(MAKE) -C 3rds/sdk clean
	-$(MAKE) -C 3rds/avcodec clean
	-$(MAKE) -C 3rds/media-server clean

# Clean everything
clean-all: clean clean-deps
	@echo "All clean!"
