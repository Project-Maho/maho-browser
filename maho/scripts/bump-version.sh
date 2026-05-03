#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"

VERSION=$(cat "$REPO_ROOT/version.txt" | tr -d '[:space:]')
echo "Bumping version to $VERSION"

# Detect OS for platform-specific tooling
IS_MACOS=false
if [[ "$(uname -s)" == "Darwin" ]]; then
    IS_MACOS=true
fi

# Cross-platform sed in-place helper
sedi() {
    if $IS_MACOS; then
        sed -i '' "$@"
    else
        sed -i "$@"
    fi
}

# macOS shell (requires PlistBuddy — macOS only)
if $IS_MACOS; then
    /usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $VERSION" "$REPO_ROOT/macos-shell/Info.plist"
    echo "  ✓ macos-shell/Info.plist"
else
    echo "  ⏭ macos-shell/Info.plist (skipped — PlistBuddy not available on Linux)"
fi

# iOS shell (requires PlistBuddy — macOS only)
if $IS_MACOS; then
    /usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $VERSION" "$REPO_ROOT/ios-shell/Info.plist"
    echo "  ✓ ios-shell/Info.plist"

    # iOS ShareExtension (if exists)
    if [ -f "$REPO_ROOT/ios-shell/ShareExtension/Info.plist" ]; then
        /usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $VERSION" "$REPO_ROOT/ios-shell/ShareExtension/Info.plist"
        echo "  ✓ ios-shell/ShareExtension/Info.plist"
    fi

    # iOS MahoWidget (if exists)
    if [ -f "$REPO_ROOT/ios-shell/MahoWidget/Info.plist" ]; then
        /usr/libexec/PlistBuddy -c "Set :CFBundleShortVersionString $VERSION" "$REPO_ROOT/ios-shell/MahoWidget/Info.plist"
        echo "  ✓ ios-shell/MahoWidget/Info.plist"
    fi
else
    echo "  ⏭ ios-shell plists (skipped — PlistBuddy not available on Linux)"
fi

# Android (cross-platform sed)
sedi "s/versionName = \".*\"/versionName = \"$VERSION\"/" "$REPO_ROOT/android-shell/app/build.gradle.kts"
echo "  ✓ android-shell/app/build.gradle.kts"

echo "Done — all targets stamped with $VERSION"
