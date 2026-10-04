import SwiftUI

struct AiSearchOverlayView: View {
    let query: String
    let isIncognito: Bool
    
    @StateObject private var viewModel: AiSearchViewModel
    @Environment(\.dismiss) private var dismiss
    @State private var activeSheet: CtaSheet?

    private enum CtaSheet: Identifiable {
        case signIn
        case apiKeys
        var id: Int { hashValue }
    }
    
    init(query: String, isIncognito: Bool) {
        self.query = query
        self.isIncognito = isIncognito
        self._viewModel = StateObject(wrappedValue: AiSearchViewModel(query: query, isIncognito: isIncognito))
    }
    
    var body: some View {
        NavigationView {
            VStack(spacing: 0) {
                // Query banner
                HStack {
                    VStack(alignment: .leading, spacing: 4) {
                        Text("Search Query")
                            .font(.system(size: 11, weight: .semibold))
                            .foregroundColor(.purple)
                            .textCase(.uppercase)
                        Text(query)
                            .font(.system(size: 17, weight: .bold))
                            .foregroundColor(.primary)
                            .lineLimit(2)
                    }
                    Spacer()
                }
                .padding(.horizontal, 20)
                .padding(.vertical, 16)
                .background(Color.purple.opacity(0.05))
                
                Divider()
                
                // Result content
                ScrollView {
                    VStack(alignment: .leading, spacing: 16) {
                        if viewModel.showByokCta {
                            byokCtaView
                        } else if let error = viewModel.errorMessage {
                            errorView(error)
                        } else {
                            if viewModel.responseText.isEmpty && viewModel.isLoading {
                                HStack {
                                    Spacer()
                                    VStack(spacing: 12) {
                                        ProgressView()
                                            .tint(.purple)
                                        Text("Thinking...")
                                            .font(.system(size: 15))
                                            .foregroundColor(.secondary)
                                    }
                                    Spacer()
                                }
                                .padding(.top, 40)
                            } else {
                                Text(viewModel.responseText)
                                    .font(.system(size: 16, weight: .regular))
                                    .foregroundColor(.primary)
                                    .lineSpacing(6)
                                    .frame(maxWidth: .infinity, alignment: .leading)
                                    .textSelection(.enabled)
                                
                                if viewModel.isLoading {
                                    HStack(spacing: 8) {
                                        ProgressView()
                                            .tint(.purple)
                                        Text("Streaming response...")
                                            .font(.system(size: 13))
                                            .foregroundColor(.secondary)
                                    }
                                    .padding(.top, 8)
                                }
                            }
                        }
                    }
                    .padding(20)
                }
            }
            .navigationTitle("Maho AI Search")
            .navigationBarTitleDisplayMode(.inline)
            .navigationBarItems(trailing: Button(action: { dismiss() }) {
                Image(systemName: "xmark.circle.fill")
                    .foregroundColor(.secondary)
                    .font(.system(size: 22))
            })
            .onAppear {
                viewModel.startSearch()
            }
            .sheet(item: $activeSheet, onDismiss: {
                viewModel.startSearch()
            }) { sheet in
                credentialSheet(for: sheet)
            }
        }
    }
    
    private var byokCtaView: some View {
        VStack(spacing: 20) {
            Image(systemName: "key.fill")
                .font(.system(size: 40))
                .foregroundColor(.purple)
                .padding(.top, 20)
            
            Text("AI Search needs credentials")
                .font(.system(size: 20, weight: .bold))
            
            Text("Sign in to your Maho account to use AI Search on free credits, or add your own OpenAI / Anthropic API key. Keys are stored securely in the iOS Keychain.")
                .font(.system(size: 15))
                .foregroundColor(.secondary)
                .multilineTextAlignment(.center)
                .lineSpacing(4)
                .padding(.horizontal, 10)
            
            Button(action: { activeSheet = .signIn }) {
                Text("Sign In for Free Credits")
                    .font(.system(size: 16, weight: .semibold))
                    .foregroundColor(.white)
                    .frame(maxWidth: .infinity)
                    .frame(height: 50)
                    .background(Color.purple)
                    .cornerRadius(12)
            }
            .padding(.top, 10)

            Button(action: { activeSheet = .apiKeys }) {
                Text("Use my own API key")
                    .font(.system(size: 15, weight: .semibold))
                    .foregroundColor(.purple)
            }
        }
        .padding(.horizontal, 10)
    }

    @ViewBuilder
    private func credentialSheet(for sheet: CtaSheet) -> some View {
        NavigationView {
            switch sheet {
            case .signIn:
                SyncSettingsView()
                    .navigationBarItems(trailing: Button("Done") { activeSheet = nil })
            case .apiKeys:
                BYOKWebView()
            }
        }
    }
    
    private func errorView(_ error: String) -> some View {
        VStack(spacing: 16) {
            Image(systemName: "exclamationmark.triangle.fill")
                .font(.system(size: 32))
                .foregroundColor(.red)
                
            Text("An error occurred")
                .font(.system(size: 17, weight: .bold))
                
            Text(error)
                .font(.system(size: 14))
                .foregroundColor(.secondary)
                .multilineTextAlignment(.center)
                
            Button(action: { viewModel.startSearch() }) {
                Text("Retry")
                    .font(.system(size: 15, weight: .semibold))
                    .foregroundColor(.purple)
                    .padding(.horizontal, 24)
                    .padding(.vertical, 10)
                    .background(Color.purple.opacity(0.1))
                    .cornerRadius(8)
            }
        }
        .frame(maxWidth: .infinity)
        .padding(.vertical, 20)
    }
}
