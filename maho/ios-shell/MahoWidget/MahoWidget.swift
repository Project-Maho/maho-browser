import WidgetKit
import SwiftUI
import UIKit
import LucideIcons

struct MahoWidgetEntry: TimelineEntry {
    let date: Date
}

struct MahoWidgetProvider: TimelineProvider {
    func placeholder(in context: Context) -> MahoWidgetEntry {
        MahoWidgetEntry(date: Date())
    }

    func getSnapshot(in context: Context, completion: @escaping (MahoWidgetEntry) -> Void) {
        completion(MahoWidgetEntry(date: Date()))
    }

    func getTimeline(in context: Context, completion: @escaping (Timeline<MahoWidgetEntry>) -> Void) {
        let entry = MahoWidgetEntry(date: Date())
        let timeline = Timeline(entries: [entry], policy: .never)
        completion(timeline)
    }
}

struct MahoWidgetEntryView: View {
    var entry: MahoWidgetProvider.Entry

    var body: some View {
        Link(destination: URL(string: "maho://search?q=")!) {
            VStack(alignment: .leading, spacing: 8) {
                HStack {
                    // `Image(lucide:)` lives in Support/MahoIcon.swift, which is a
                    // member of the `Maho` app target only — not this extension.
                    // Use the LucideIcons public API directly, matching that
                    // helper's behavior (template rendering so `foregroundStyle`
                    // tints the glyph).
                    Image(uiImage: Lucide.search.withRenderingMode(.alwaysTemplate))
                        .foregroundStyle(.secondary)
                    Text("Search in Maho")
                        .font(.subheadline)
                        .foregroundStyle(.primary)
                    Spacer()
                }
                .padding(.horizontal, 12)
                .padding(.vertical, 10)
                .background(Color.gray.opacity(0.15))
                .clipShape(RoundedRectangle(cornerRadius: 12, style: .continuous))

                Spacer()
            }
            .padding(12)
        }
    }
}

@main
struct MahoWidget: Widget {
    let kind: String = "MahoWidget"

    var body: some WidgetConfiguration {
        StaticConfiguration(kind: kind, provider: MahoWidgetProvider()) { entry in
            MahoWidgetEntryView(entry: entry)
        }
        .configurationDisplayName("Maho Search")
        .description("Quickly search the web with Maho.")
        .supportedFamilies([.systemSmall, .systemMedium])
    }
}
