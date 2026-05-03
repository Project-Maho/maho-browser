import SwiftUI

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
            Image(systemName: "chevron.backward")
                .font(.title3)
                .frame(width: 44, height: 44)
        }
        .disabled(!viewModel.canGoBack)
    }

    private var forwardButton: some View {
        Button {
            viewModel.goForward()
        } label: {
            Image(systemName: "chevron.forward")
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
            Image(systemName: viewModel.isLoading ? "xmark" : "arrow.clockwise")
                .font(.title3)
                .frame(width: 44, height: 44)
        }
    }

    private var tabsPlaceholder: some View {
        Button {
        } label: {
            Image(systemName: "square.on.square")
                .font(.title3)
                .frame(width: 44, height: 44)
        }
    }
}
