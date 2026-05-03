import Foundation
import Network

final class MCPServer {
    static let shared = MCPServer()

    private var listener: NWListener?
    private var connections: [UUID: MCPConnection] = [:]
    private(set) var isRunning = false
    private let bearerToken: String
    private let port: UInt16 = 9851
    private let queue = DispatchQueue(label: "dev.maho.mcp-server")

    let toolRegistry = MCPToolRegistry()

    init() {
        if let saved = UserDefaults.standard.string(forKey: "maho.mcp.token") {
            bearerToken = saved
        } else {
            let token = UUID().uuidString
            UserDefaults.standard.set(token, forKey: "maho.mcp.token")
            bearerToken = token
        }
    }

    func start() {
        guard !isRunning else { return }

        do {
            let params = NWParameters.tcp
            params.allowLocalEndpointReuse = true
            listener = try NWListener(using: params, on: NWEndpoint.Port(rawValue: port)!)
        } catch {
            NSLog("[MCPServer] Failed to create listener: \(error)")
            return
        }

        listener?.stateUpdateHandler = { [weak self] state in
            switch state {
            case .ready:
                NSLog("[MCPServer] Listening on localhost:\(self?.port ?? 0)")
                DispatchQueue.main.async { self?.isRunning = true }
            case .failed(let error):
                NSLog("[MCPServer] Listener failed: \(error)")
                DispatchQueue.main.async { self?.isRunning = false }
            case .cancelled:
                DispatchQueue.main.async { self?.isRunning = false }
            default:
                break
            }
        }

        listener?.newConnectionHandler = { [weak self] nwConnection in
            self?.handleNewConnection(nwConnection)
        }

        listener?.start(queue: queue)
    }

    func stop() {
        listener?.cancel()
        listener = nil
        for conn in connections.values {
            conn.close()
        }
        connections.removeAll()
        isRunning = false
    }

    func configJSON() -> String {
        return """
        {
          "mcpServers": {
            "maho-browser": {
              "url": "http://localhost:\(port)/sse",
              "headers": {
                "Authorization": "Bearer \(bearerToken)"
              }
            }
          }
        }
        """
    }

    private func handleNewConnection(_ nwConnection: NWConnection) {
        // Only allow localhost
        if let endpoint = nwConnection.currentPath?.remoteEndpoint,
           case .hostPort(let host, _) = endpoint {
            let hostStr = "\(host)"
            if hostStr != "127.0.0.1" && hostStr != "::1" && hostStr != "localhost" {
                nwConnection.cancel()
                return
            }
        }

        let id = UUID()
        let conn = MCPConnection(
            id: id,
            connection: nwConnection,
            bearerToken: bearerToken,
            toolRegistry: toolRegistry
        )
        connections[id] = conn

        conn.onClose = { [weak self] in
            self?.connections.removeValue(forKey: id)
        }

        conn.start(queue: queue)
    }
}
