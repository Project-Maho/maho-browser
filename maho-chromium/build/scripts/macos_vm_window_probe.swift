#!/usr/bin/env swift

import CoreGraphics
import Darwin
import Foundation


private let schemaVersion = 1
private let visibleOwners = Set(["maho", "tart"])


private func number(_ value: Any?) -> NSNumber? {
    value as? NSNumber
}


private func fail(_ message: String) -> Never {
    let line = "macos_vm_window_probe: \(message)\n"
    FileHandle.standardError.write(Data(line.utf8))
    exit(EXIT_FAILURE)
}


let options: CGWindowListOption = [
    .optionOnScreenOnly,
    .excludeDesktopElements,
]

guard let windowInfo = CGWindowListCopyWindowInfo(
    options,
    kCGNullWindowID
) as? [[String: Any]] else {
    fail("CoreGraphics did not return a window list")
}

var windows: [[String: Any]] = []
for window in windowInfo {
    guard
        let ownerName = window[kCGWindowOwnerName as String] as? String,
        visibleOwners.contains(ownerName.lowercased()),
        let ownerPID = number(window[kCGWindowOwnerPID as String])?.intValue,
        let windowID = number(window[kCGWindowNumber as String])?.intValue,
        let layer = number(window[kCGWindowLayer as String])?.intValue,
        let rawBounds = window[kCGWindowBounds as String] as? [String: Any],
        let x = number(rawBounds["X"])?.doubleValue,
        let y = number(rawBounds["Y"])?.doubleValue,
        let width = number(rawBounds["Width"])?.doubleValue,
        let height = number(rawBounds["Height"])?.doubleValue
    else {
        continue
    }

    // CGWindowListCopyWindowInfo exposes metadata only. Unlike image capture
    // and Accessibility APIs, this does not request a TCC entitlement.
    let alpha = number(window[kCGWindowAlpha as String])?.doubleValue ?? 1.0
    guard width > 0, height > 0, alpha > 0 else {
        continue
    }

    windows.append([
        "owner_name": ownerName,
        "owner_pid": ownerPID,
        "window_id": windowID,
        "layer": layer,
        "bounds": [
            "x": x,
            "y": y,
            "width": width,
            "height": height,
        ],
    ])
}

windows.sort { left, right in
    let leftOwner = (left["owner_name"] as? String ?? "").lowercased()
    let rightOwner = (right["owner_name"] as? String ?? "").lowercased()
    if leftOwner != rightOwner {
        return leftOwner < rightOwner
    }

    let leftPID = number(left["owner_pid"])?.intValue ?? 0
    let rightPID = number(right["owner_pid"])?.intValue ?? 0
    if leftPID != rightPID {
        return leftPID < rightPID
    }

    let leftWindowID = number(left["window_id"])?.intValue ?? 0
    let rightWindowID = number(right["window_id"])?.intValue ?? 0
    return leftWindowID < rightWindowID
}

let payload: [String: Any] = [
    "schema_version": schemaVersion,
    "windows": windows,
]

guard JSONSerialization.isValidJSONObject(payload) else {
    fail("CoreGraphics returned values that cannot be encoded as JSON")
}

do {
    let data = try JSONSerialization.data(
        withJSONObject: payload,
        options: [.sortedKeys]
    )
    FileHandle.standardOutput.write(data)
    FileHandle.standardOutput.write(Data("\n".utf8))
} catch {
    fail("failed to encode JSON: \(error)")
}
