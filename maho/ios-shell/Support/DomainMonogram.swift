import SwiftUI

struct DomainMonogram {
    static func letter(for host: String) -> Character {
        let cleaned = host.lowercased().replacingOccurrences(of: "www.", with: "")
        return cleaned.first?.uppercased().first ?? "?"
    }
    
    static func color(for host: String) -> Color {
        let cleaned = host.lowercased().replacingOccurrences(of: "www.", with: "")
        var hash: UInt = 0
        for char in cleaned.utf8 {
            hash = UInt(char) &+ (hash << 6) &+ (hash << 16) &- hash
        }
        let hue = Double(hash % 360) / 360.0
        return Color(hue: hue, saturation: 0.65, brightness: 0.6)
    }
}
