import Foundation

enum TextDirection: String, Codable {
    case ltr
    case rtl
    case auto

    init(readabilityDir: String?) {
        switch readabilityDir?.lowercased() {
        case "ltr": self = .ltr
        case "rtl": self = .rtl
        default: self = .auto
        }
    }
}

struct ExtractedArticle: Codable, Identifiable {
    let id: UUID
    let title: String
    let content: String
    let textContent: String
    let byline: String
    let textDirection: TextDirection
    let excerpt: String?
    let siteName: String?
    let length: Int?
    let publishedTime: String?

    init(
        id: UUID = UUID(),
        title: String,
        content: String,
        textContent: String,
        byline: String,
        textDirection: TextDirection,
        excerpt: String? = nil,
        siteName: String? = nil,
        length: Int? = nil,
        publishedTime: String? = nil
    ) {
        self.id = id
        self.title = title
        self.content = content
        self.textContent = textContent
        self.byline = byline
        self.textDirection = textDirection
        self.excerpt = excerpt
        self.siteName = siteName
        self.length = length
        self.publishedTime = publishedTime
    }
}
