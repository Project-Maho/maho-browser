import SwiftUI
import UIKit
import WebKit

struct RefreshControlView: UIViewRepresentable {
    @ObservedObject var viewModel: BrowserViewModel
    let onRefresh: () -> Void

    func makeCoordinator() -> RefreshCoordinator {
        RefreshCoordinator(viewModel: viewModel, onRefresh: onRefresh)
    }

    func makeUIView(context: Context) -> UIRefreshControl {
        let control = UIRefreshControl()
        control.addTarget(
            context.coordinator,
            action: #selector(RefreshCoordinator.handleRefresh(_:)),
            for: .valueChanged
        )
        return control
    }

    func updateUIView(_ uiView: UIRefreshControl, context: Context) {
        if !viewModel.isLoading && uiView.isRefreshing {
            uiView.endRefreshing()
        }
    }
}

final class RefreshCoordinator: NSObject {
    private let viewModel: BrowserViewModel
    private let onRefresh: () -> Void

    init(viewModel: BrowserViewModel, onRefresh: @escaping () -> Void) {
        self.viewModel = viewModel
        self.onRefresh = onRefresh
        super.init()
    }

    @MainActor
    @objc func handleRefresh(_ sender: UIRefreshControl) {
        onRefresh()

        Task {
            try? await Task.sleep(nanoseconds: 3_000_000_000)
            if sender.isRefreshing {
                sender.endRefreshing()
            }
        }
    }
}
