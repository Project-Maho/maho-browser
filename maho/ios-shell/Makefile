# Maho iOS Shell — Build Infrastructure
# Requires: Rust toolchain with iOS targets, cbindgen, Xcode

RUST_TARGET_IOS    := aarch64-apple-ios
RUST_TARGET_SIM    := aarch64-apple-ios-sim
RUST_LIB_NAME      := libmaho_ffi.a
RUST_LIB_DIR       := ../target
HEADER_DIR         := include
FFI_CRATE          := maho-ffi
CBINDGEN_CONFIG    := ../mobile-build/cbindgen.toml
UNIVERSAL_LIB_DIR  := build/universal

.PHONY: all build-rust-ios build-rust-sim generate-header universal-lib build-app test clean

all: generate-header universal-lib

# Build the Rust FFI library for iOS device (arm64)
build-rust-ios:
	cargo build --target $(RUST_TARGET_IOS) --release -p $(FFI_CRATE) --manifest-path ../Cargo.toml

# Build the Rust FFI library for iOS simulator (arm64)
build-rust-sim:
	cargo build --target $(RUST_TARGET_SIM) --release -p $(FFI_CRATE) --manifest-path ../Cargo.toml

# Generate the C header from Rust FFI crate
generate-header:
	cbindgen --config $(CBINDGEN_CONFIG) --crate $(FFI_CRATE) --output $(HEADER_DIR)/maho_ffi.h

# Create universal binary for device + simulator
universal-lib: build-rust-ios build-rust-sim
	@mkdir -p $(UNIVERSAL_LIB_DIR)
	lipo -create \
		$(RUST_LIB_DIR)/$(RUST_TARGET_IOS)/release/$(RUST_LIB_NAME) \
		$(RUST_LIB_DIR)/$(RUST_TARGET_SIM)/release/$(RUST_LIB_NAME) \
		-output $(UNIVERSAL_LIB_DIR)/$(RUST_LIB_NAME)
	@echo "Universal library created at $(UNIVERSAL_LIB_DIR)/$(RUST_LIB_NAME)"

# Build the iOS app via xcodebuild (requires .xcodeproj)
build-app: generate-header
	xcodebuild build \
		-project Maho.xcodeproj \
		-scheme Maho \
		-sdk iphoneos \
		-configuration Release \
		HEADER_SEARCH_PATHS="$$(pwd)/$(HEADER_DIR)" \
		LIBRARY_SEARCH_PATHS="$$(pwd)/$(UNIVERSAL_LIB_DIR)"

# Run tests
test:
	xcodebuild test \
		-project Maho.xcodeproj \
		-scheme Maho \
		-sdk iphonesimulator \
		-destination 'platform=iOS Simulator,name=iPhone 15'

# Clean build artifacts
clean:
	rm -rf build/
	rm -f $(HEADER_DIR)/maho_ffi.h
	cargo clean --target $(RUST_TARGET_IOS) --manifest-path ../Cargo.toml
	cargo clean --target $(RUST_TARGET_SIM) --manifest-path ../Cargo.toml
