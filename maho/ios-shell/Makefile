# Maho iOS Shell — Build Infrastructure
# Requires: Rust toolchain with iOS targets, cbindgen, Xcode

RUST_TARGET_IOS    := aarch64-apple-ios
RUST_TARGET_SIM    := aarch64-apple-ios-sim
# Must match project.yml deploymentTarget. Without it the Rust build links against
# the old default (iOS 10.0) while the SDK is new, causing sqlcipher's sqlite3.c to
# reference ___chkstk_darwin with no runtime support -> undefined-symbol link failure.
IOS_DEPLOYMENT_TARGET := 17.0
RUST_LIB_NAME      := libmaho_ffi.a
RUST_LIB_DIR       := ../target
HEADER_DIR         := include
FFI_CRATE          := maho-ffi
CBINDGEN_CONFIG    := cbindgen.toml
IOS_LIB_DIR        := build/ios
SIM_LIB_DIR        := build/sim
UNIVERSAL_LIB_DIR  := build/universal

.PHONY: all build-rust-ios build-rust-sim generate-header universal-lib build-app test clean

all: generate-header universal-lib

build-rust-ios:
	IPHONEOS_DEPLOYMENT_TARGET=$(IOS_DEPLOYMENT_TARGET) cargo build --target $(RUST_TARGET_IOS) --release -p $(FFI_CRATE) --manifest-path ../Cargo.toml

build-rust-sim:
	IPHONEOS_DEPLOYMENT_TARGET=$(IOS_DEPLOYMENT_TARGET) cargo build --target $(RUST_TARGET_SIM) --release -p $(FFI_CRATE) --manifest-path ../Cargo.toml

generate-header:
	cbindgen --config $(CBINDGEN_CONFIG) --crate $(FFI_CRATE) --output $(HEADER_DIR)/maho_ffi.h ..

universal-lib: build-rust-ios build-rust-sim
	@mkdir -p $(IOS_LIB_DIR) $(SIM_LIB_DIR) $(UNIVERSAL_LIB_DIR)
	cp $(RUST_LIB_DIR)/$(RUST_TARGET_IOS)/release/$(RUST_LIB_NAME) $(IOS_LIB_DIR)/$(RUST_LIB_NAME)
	cp $(RUST_LIB_DIR)/$(RUST_TARGET_SIM)/release/$(RUST_LIB_NAME) $(SIM_LIB_DIR)/$(RUST_LIB_NAME)
	cp $(RUST_LIB_DIR)/$(RUST_TARGET_SIM)/release/$(RUST_LIB_NAME) $(UNIVERSAL_LIB_DIR)/$(RUST_LIB_NAME)
	@echo "Per-platform libs: $(IOS_LIB_DIR)/, $(SIM_LIB_DIR)/  (project.yml LIBRARY_SEARCH_PATHS is SDK-conditional)"
	@echo "Legacy universal fallback: $(UNIVERSAL_LIB_DIR)/$(RUST_LIB_NAME) (sim slice, for tools that only look here)"


build-app:
	xcodebuild build \
		-project Maho.xcodeproj \
		-scheme Maho \
		-sdk iphoneos \
		-configuration Release \
		HEADER_SEARCH_PATHS="$$(pwd)/$(HEADER_DIR)"

test:
	xcodebuild test \
		-project Maho.xcodeproj \
		-scheme Maho \
		-sdk iphonesimulator \
		-destination 'platform=iOS Simulator,name=TestiPhone16Pro'

# Clean build artifacts
clean:
	rm -rf build/
	rm -f $(HEADER_DIR)/maho_ffi.h
	cargo clean --target $(RUST_TARGET_IOS) --manifest-path ../Cargo.toml
	cargo clean --target $(RUST_TARGET_SIM) --manifest-path ../Cargo.toml
