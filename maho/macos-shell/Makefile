SHELL := /bin/bash
APP_NAME := Maho_Swift
BUILD_DIR := build
RUST_TARGET_DIR := ../target/release
FFI_LIB := $(RUST_TARGET_DIR)/libmaho_ffi.a
BRIDGING_HEADER := Maho-Bridging-Header.h
HEADER_DIR := include

# Find all Swift source files
SWIFT_SOURCES := $(shell find Sources -name '*.swift')

# Frameworks
FRAMEWORKS := -framework AppKit -framework WebKit -framework Security -framework AuthenticationServices -framework UniformTypeIdentifiers

# Linker flags for Rust static lib dependencies
RUST_LINK_FLAGS := -L$(RUST_TARGET_DIR) -lmaho_ffi -lSystem -lc -lresolv -lm -lsqlite3

# Swift flags
SWIFT_FLAGS := -import-objc-header $(BRIDGING_HEADER) -I$(HEADER_DIR) -O -whole-module-optimization

.PHONY: all clean rust app run test uitest release

all: app

rust:
	cargo build --release -p maho-ffi --manifest-path ../Cargo.toml

$(FFI_LIB): rust

app: $(FFI_LIB) $(SWIFT_SOURCES) $(BRIDGING_HEADER)
	@mkdir -p $(BUILD_DIR)/$(APP_NAME).app/Contents/MacOS
	@mkdir -p $(BUILD_DIR)/$(APP_NAME).app/Contents/Resources
	@cp Info.plist $(BUILD_DIR)/$(APP_NAME).app/Contents/
	@cp AppIcon.icns $(BUILD_DIR)/$(APP_NAME).app/Contents/Resources/
	swiftc $(SWIFT_FLAGS) $(SWIFT_SOURCES) \
		$(FRAMEWORKS) $(RUST_LINK_FLAGS) \
		-o $(BUILD_DIR)/$(APP_NAME).app/Contents/MacOS/$(APP_NAME)
	@echo "BUILD SUCCEEDED: $(BUILD_DIR)/$(APP_NAME).app"

run: app
	@open $(BUILD_DIR)/$(APP_NAME).app

TEST_SOURCES := $(shell find Tests -name '*.swift')
TEST_SUPPORT_SOURCES := $(filter %StandaloneTestSupport.swift,$(TEST_SOURCES))
TEST_CASE_SOURCES := $(filter-out $(TEST_SUPPORT_SOURCES),$(TEST_SOURCES))
SWIFT_SOURCES_NO_MAIN := $(filter-out Sources/main.swift,$(SWIFT_SOURCES))

test: $(FFI_LIB) $(SWIFT_SOURCES) $(TEST_SOURCES)
	@mkdir -p $(BUILD_DIR)
	@for test in $(TEST_CASE_SOURCES); do \
		echo "Running $$test..."; \
		swiftc $(SWIFT_FLAGS) $(SWIFT_SOURCES_NO_MAIN) $(TEST_SUPPORT_SOURCES) $$test \
			$(FRAMEWORKS) $(RUST_LINK_FLAGS) \
			-o $(BUILD_DIR)/test_runner && \
		./$(BUILD_DIR)/test_runner || exit 1; \
	done
	@echo "ALL TESTS PASSED"

uitest: app
	@command -v xcodegen >/dev/null 2>&1 || { echo "xcodegen required: brew install xcodegen"; exit 1; }
	xcodegen generate
	xcodebuild test -project Maho.xcodeproj -scheme MahoUITests -destination 'platform=macOS' \
		CODE_SIGN_IDENTITY="-" CODE_SIGNING_REQUIRED=NO CODE_SIGN_STYLE=Manual DEVELOPMENT_TEAM= \
		OTHER_CODE_SIGN_FLAGS="--deep" CODE_SIGN_INJECT_BASE_ENTITLEMENTS=NO | xcpretty || true

release: rust
	@command -v xcodegen >/dev/null 2>&1 || { echo "xcodegen required: brew install xcodegen"; exit 1; }
	xcodegen generate
	xcodebuild archive -project Maho.xcodeproj -scheme Maho -configuration Release \
		-archivePath $(BUILD_DIR)/Maho.xcarchive \
		CODE_SIGN_IDENTITY="Developer ID Application" \
		ENABLE_HARDENED_RUNTIME=YES \
		CODE_SIGN_ENTITLEMENTS=Maho.entitlements | xcpretty || true
	@echo "ARCHIVE SUCCEEDED: $(BUILD_DIR)/Maho.xcarchive"

clean:
	rm -rf $(BUILD_DIR)
