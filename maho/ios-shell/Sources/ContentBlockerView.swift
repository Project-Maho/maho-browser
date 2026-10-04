import SwiftUI

struct ContentBlockerView: View {
    @ObservedObject var state: BrowserState
    @Environment(\.dismiss) private var dismiss

    var body: some View {
        NavigationStack {
            List {
                Section {
                    Toggle("Enable Content Blocker", isOn: $state.contentBlockerEnabled)
                        .accessibilityIdentifier("contentBlockerToggle")
                }

                Section("Filter Lists") {
                    ForEach(state.contentRules, id: \.id) { rule in
                        Toggle(rule.name, isOn: binding(for: rule))
                    }
                }
            }
            .navigationTitle("Content Blocker")
            .toolbar {
                SwiftUI.ToolbarItem(placement: .confirmationAction) {
                    Button("Done") { dismiss() }
                        .accessibilityIdentifier("contentBlockerDoneButton")
                }
            }
        }
        .accessibilityIdentifier("contentBlockerView")
    }

    private func binding(for rule: ContentRule) -> Binding<Bool> {
        Binding(
            get: { state.enabledRuleIds.contains(rule.id) },
            set: { enabled in
                if enabled {
                    state.enabledRuleIds.insert(rule.id)
                } else {
                    state.enabledRuleIds.remove(rule.id)
                }
                state.updateContentRules()
            }
        )
    }
}

struct ContentRule: Identifiable, Codable {
    let id: String
    let name: String
}
