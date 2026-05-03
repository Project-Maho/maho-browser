import Foundation

final class GitHubPRProvider: LiveFolderProvider {
    let providerId = "github_prs"
    let displayName = "GitHub Pull Requests"
    let iconName = "arrow.triangle.pull"
    let refreshInterval: TimeInterval = 300

    func fetchItems(config: [String: String], completion: @escaping ([LiveFolderItem]) -> Void) {
        guard let repo = config["repo"] else {
            completion([])
            return
        }
        let urlString = "https://api.github.com/repos/\(repo)/pulls?state=open&per_page=20"
        guard let url = URL(string: urlString) else {
            completion([])
            return
        }

        var request = URLRequest(url: url)
        request.setValue("application/vnd.github.v3+json", forHTTPHeaderField: "Accept")
        if let token = config["token"] {
            request.setValue("Bearer \(token)", forHTTPHeaderField: "Authorization")
        }

        URLSession.shared.dataTask(with: request) { data, _, error in
            guard let data = data, error == nil else {
                DispatchQueue.main.async { completion([]) }
                return
            }

            let items: [LiveFolderItem]
            if let prs = try? JSONSerialization.jsonObject(with: data) as? [[String: Any]] {
                items = prs.compactMap { pr in
                    guard let title = pr["title"] as? String,
                          let htmlUrl = pr["html_url"] as? String,
                          let number = pr["number"] as? Int,
                          let user = pr["user"] as? [String: Any],
                          let login = user["login"] as? String else { return nil }
                    return LiveFolderItem(
                        id: "pr_\(number)",
                        title: "#\(number) \(title)",
                        url: htmlUrl,
                        subtitle: login,
                        badgeText: nil,
                        icon: "arrow.triangle.pull"
                    )
                }
            } else {
                items = []
            }
            DispatchQueue.main.async { completion(items) }
        }.resume()
    }
}
