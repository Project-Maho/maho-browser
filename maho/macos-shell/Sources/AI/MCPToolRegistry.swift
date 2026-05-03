import AppKit
import WebKit

protocol MCPToolRegistryDelegate: AnyObject {
    func mcpActiveWebView() -> WKWebView?
    func mcpWebView(forTabId: String) -> WKWebView?
    func mcpNavigate(to url: String)
    func mcpCloseTab(id: String)
    func mcpCreateTab(url: String)
}

struct MCPToolDefinition {
    let name: String
    let description: String
    let inputSchema: [String: Any]
    let isReadOnly: Bool
    let handler: ([String: Any], @escaping (Result<Any, Error>) -> Void) -> Void
}

final class MCPToolRegistry {
    private var tools: [String: MCPToolDefinition] = [:]
    weak var delegate: MCPToolRegistryDelegate?

    init() {
        registerBuiltinTools()
    }

    func listTools() -> [[String: Any]] {
        return tools.values.map { tool in
            return [
                "name": tool.name,
                "description": tool.description,
                "inputSchema": tool.inputSchema
            ]
        }
    }

    func callTool(name: String, arguments: [String: Any], completion: @escaping (Result<Any, Error>) -> Void) {
        guard let tool = tools[name] else {
            completion(.failure(MCPError.toolNotFound(name)))
            return
        }

        if tool.isReadOnly {
            tool.handler(arguments, completion)
        } else {
            MCPPermissionPrompt.requestPermission(toolName: name, params: arguments) { allowed in
                if allowed {
                    tool.handler(arguments, completion)
                } else {
                    completion(.failure(MCPError.permissionDenied))
                }
            }
        }
    }

    private func register(_ tool: MCPToolDefinition) {
        tools[tool.name] = tool
    }

    private func registerBuiltinTools() {
        register(MCPToolDefinition(
            name: "list_tabs",
            description: "List all open tabs with their IDs, titles, URLs, and states",
            inputSchema: ["type": "object", "properties": [:] as [String: Any], "required": [] as [String]],
            isReadOnly: true,
            handler: { [weak self] _, completion in
                self?.handleListTabs(completion: completion) ?? completion(.failure(MCPError.serverUnavailable))
            }
        ))

        register(MCPToolDefinition(
            name: "get_active_tab",
            description: "Get information about the currently active tab",
            inputSchema: ["type": "object", "properties": [:] as [String: Any], "required": [] as [String]],
            isReadOnly: true,
            handler: { [weak self] _, completion in
                self?.handleGetActiveTab(completion: completion) ?? completion(.failure(MCPError.serverUnavailable))
            }
        ))

        register(MCPToolDefinition(
            name: "get_page_content",
            description: "Get the text content of the active tab's page",
            inputSchema: ["type": "object", "properties": [:] as [String: Any], "required": [] as [String]],
            isReadOnly: true,
            handler: { [weak self] _, completion in
                self?.handleGetPageContent(completion: completion) ?? completion(.failure(MCPError.serverUnavailable))
            }
        ))

        register(MCPToolDefinition(
            name: "get_bookmarks",
            description: "Get all bookmarked (favorited and pinned) tabs",
            inputSchema: ["type": "object", "properties": [:] as [String: Any], "required": [] as [String]],
            isReadOnly: true,
            handler: { [weak self] _, completion in
                self?.handleGetBookmarks(completion: completion) ?? completion(.failure(MCPError.serverUnavailable))
            }
        ))

        register(MCPToolDefinition(
            name: "get_downloads",
            description: "Get the list of downloads with their status",
            inputSchema: ["type": "object", "properties": [:] as [String: Any], "required": [] as [String]],
            isReadOnly: true,
            handler: { [weak self] _, completion in
                self?.handleGetDownloads(completion: completion) ?? completion(.failure(MCPError.serverUnavailable))
            }
        ))

        register(MCPToolDefinition(
            name: "open_tab",
            description: "Open a new tab with the specified URL",
            inputSchema: [
                "type": "object",
                "properties": ["url": ["type": "string", "description": "The URL to open"]],
                "required": ["url"]
            ] as [String: Any],
            isReadOnly: false,
            handler: { [weak self] args, completion in
                self?.handleOpenTab(args: args, completion: completion) ?? completion(.failure(MCPError.serverUnavailable))
            }
        ))

        register(MCPToolDefinition(
            name: "close_tab",
            description: "Close a tab by its ID",
            inputSchema: [
                "type": "object",
                "properties": ["tab_id": ["type": "string", "description": "The ID of the tab to close"]],
                "required": ["tab_id"]
            ] as [String: Any],
            isReadOnly: false,
            handler: { [weak self] args, completion in
                self?.handleCloseTab(args: args, completion: completion) ?? completion(.failure(MCPError.serverUnavailable))
            }
        ))

        register(MCPToolDefinition(
            name: "navigate_to",
            description: "Navigate the active tab to a new URL",
            inputSchema: [
                "type": "object",
                "properties": ["url": ["type": "string", "description": "The URL to navigate to"]],
                "required": ["url"]
            ] as [String: Any],
            isReadOnly: false,
            handler: { [weak self] args, completion in
                self?.handleNavigateTo(args: args, completion: completion) ?? completion(.failure(MCPError.serverUnavailable))
            }
        ))

        register(MCPToolDefinition(
            name: "search_history",
            description: "Search the user's browsing history by keyword. Returns matching URLs, titles, and visit times.",
            inputSchema: [
                "type": "object",
                "properties": [
                    "query": ["type": "string", "description": "Search keyword to find in history titles and URLs"],
                    "limit": ["type": "integer", "description": "Maximum number of results (default: 20)"]
                ] as [String: Any],
                "required": ["query"]
            ] as [String: Any],
            isReadOnly: true,
            handler: { [weak self] args, completion in
                self?.handleSearchHistory(args: args, completion: completion) ?? completion(.failure(MCPError.serverUnavailable))
            }
        ))
    }

    // MARK: - Read Tool Handlers

    private func handleListTabs(completion: @escaping (Result<Any, Error>) -> Void) {
        let tabs = MahoBridge.shared.getTabViewModels()
        let activeTabId = (NSApp.delegate as? AppDelegate)?.mainWindowControllerRef?.activeTabId
        let result: [[String: Any]] = tabs.map { tab in
            return [
                "id": tab.id,
                "title": tab.title,
                "url": tab.url,
                "isActive": tab.id == activeTabId,
                "isPinned": tab.isPinned,
                "isFavorite": tab.isFavorite,
                "isLoading": tab.isLoading
            ]
        }
        completion(.success(result))
    }

    private func handleGetActiveTab(completion: @escaping (Result<Any, Error>) -> Void) {
        guard let activeTabId = (NSApp.delegate as? AppDelegate)?.mainWindowControllerRef?.activeTabId else {
            completion(.failure(MCPError.noActiveTab))
            return
        }
        let tabs = MahoBridge.shared.getTabViewModels()
        guard let tab = tabs.first(where: { $0.id == activeTabId }) else {
            completion(.failure(MCPError.noActiveTab))
            return
        }
        completion(.success(["id": tab.id, "title": tab.title, "url": tab.url]))
    }

    private func handleGetPageContent(completion: @escaping (Result<Any, Error>) -> Void) {
        DispatchQueue.main.async { [weak self] in
            guard let webView = self?.delegate?.mcpActiveWebView() else {
                completion(.failure(MCPError.noActiveTab))
                return
            }
            PageContextExtractor.extractText(from: webView) { context in
                completion(.success([
                    "title": context.title,
                    "url": context.url,
                    "content": context.text
                ]))
            }
        }
    }

    private func handleGetBookmarks(completion: @escaping (Result<Any, Error>) -> Void) {
        let tabs = MahoBridge.shared.getTabViewModels()
        let bookmarks = tabs.filter { $0.isPinned || $0.isFavorite }
        let result: [[String: Any]] = bookmarks.map { tab in
            return [
                "id": tab.id,
                "title": tab.title,
                "url": tab.url,
                "isPinned": tab.isPinned,
                "isFavorite": tab.isFavorite
            ]
        }
        completion(.success(result))
    }

    private func handleGetDownloads(completion: @escaping (Result<Any, Error>) -> Void) {
        let downloads = MahoBridge.shared.getDownloadViewModels()
        let result: [[String: Any]] = downloads.map { dl in
            return [
                "id": dl.id,
                "filename": dl.filename,
                "url": dl.url,
                "status": dl.state.rawValue,
                "totalBytes": dl.totalBytes,
                "receivedBytes": dl.receivedBytes
            ]
        }
        completion(.success(result))
    }

    private func handleSearchHistory(args: [String: Any], completion: @escaping (Result<Any, Error>) -> Void) {
        guard let query = args["query"] as? String else {
            completion(.failure(MCPError.invalidArguments("Missing 'query' parameter")))
            return
        }
        let limit = (args["limit"] as? Int) ?? 20

        guard let jsonString = MahoBridge.shared.searchHistory(query: query, limit: UInt(max(1, limit))) else {
            completion(.success([]))
            return
        }

        guard let data = jsonString.data(using: .utf8),
              let results = try? JSONSerialization.jsonObject(with: data) else {
            completion(.success([]))
            return
        }
        completion(.success(results))
    }

    // MARK: - LLM Integration

    func toolDefinitionsForLLM(providerType: LLMProviderType) -> [[String: Any]] {
        switch providerType {
        case .openai, .localServer, .remoteServer:
            return tools.values.map { tool in
                [
                    "type": "function",
                    "function": [
                        "name": tool.name,
                        "description": tool.description,
                        "parameters": tool.inputSchema
                    ] as [String: Any]
                ]
            }
        case .anthropic:
            return tools.values.map { tool in
                [
                    "name": tool.name,
                    "description": tool.description,
                    "input_schema": tool.inputSchema
                ]
            }
        }
    }

    func callToolAsync(name: String, arguments: [String: Any]) async throws -> Any {
        try await withCheckedThrowingContinuation { continuation in
            callTool(name: name, arguments: arguments) { result in
                switch result {
                case .success(let value):
                    continuation.resume(returning: value)
                case .failure(let error):
                    continuation.resume(throwing: error)
                }
            }
        }
    }

    // MARK: - Write Tool Handlers

    private func handleOpenTab(args: [String: Any], completion: @escaping (Result<Any, Error>) -> Void) {
        guard let url = args["url"] as? String else {
            completion(.failure(MCPError.invalidArguments("Missing 'url' parameter")))
            return
        }
        DispatchQueue.main.async { [weak self] in
            self?.delegate?.mcpCreateTab(url: url)
            completion(.success(["status": "ok", "url": url]))
        }
    }

    private func handleCloseTab(args: [String: Any], completion: @escaping (Result<Any, Error>) -> Void) {
        guard let tabId = args["tab_id"] as? String else {
            completion(.failure(MCPError.invalidArguments("Missing 'tab_id' parameter")))
            return
        }
        DispatchQueue.main.async { [weak self] in
            self?.delegate?.mcpCloseTab(id: tabId)
            completion(.success(["status": "ok", "tab_id": tabId]))
        }
    }

    private func handleNavigateTo(args: [String: Any], completion: @escaping (Result<Any, Error>) -> Void) {
        guard let url = args["url"] as? String else {
            completion(.failure(MCPError.invalidArguments("Missing 'url' parameter")))
            return
        }
        DispatchQueue.main.async { [weak self] in
            self?.delegate?.mcpNavigate(to: url)
            completion(.success(["status": "ok", "url": url]))
        }
    }
}

enum MCPError: LocalizedError {
    case toolNotFound(String)
    case permissionDenied
    case noActiveTab
    case invalidArguments(String)
    case serverUnavailable

    var errorDescription: String? {
        switch self {
        case .toolNotFound(let name): return "Tool not found: \(name)"
        case .permissionDenied: return "User denied permission"
        case .noActiveTab: return "No active tab"
        case .invalidArguments(let msg): return "Invalid arguments: \(msg)"
        case .serverUnavailable: return "MCP server unavailable"
        }
    }
}
