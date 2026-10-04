import SwiftUI
import LucideIcons

struct NavigationButtonsView: View {
    @ObservedObject var viewModel: BrowserViewModel

    var body: some View {
        HStack(spacing: 0) {
            backButton
            Spacer()
            forwardButton
            Spacer()
            reloadStopButton
            Spacer()
            tabsPlaceholder
        }
    }

    private var backButton: some View {
        Button {
            viewModel.goBack()
        } label: {
            Image(lucide: Lucide.chevronLeft)
                .font(.title3)
                .frame(width: 44, height: 44)
        }
        .disabled(!viewModel.canGoBack)
    }

    private var forwardButton: some View {
        Button {
            viewModel.goForward()
        } label: {
            Image(lucide: Lucide.chevronRight)
                .font(.title3)
                .frame(width: 44, height: 44)
        }
        .disabled(!viewModel.canGoForward)
    }

    private var reloadStopButton: some View {
        Button {
            if viewModel.isLoading {
                viewModel.stop()
            } else {
                viewModel.reload()
            }
        } label: {
            Image(lucide: viewModel.isLoading ? Lucide.x : Lucide.rotateCw)
                .font(.title3)
                .frame(width: 44, height: 44)
        }
    }

    private var tabsPlaceholder: some View {
        Button {
        } label: {
            Image(lucide: Lucide.copy)
                .font(.title3)
                .frame(width: 44, height: 44)
        }
    }
}
