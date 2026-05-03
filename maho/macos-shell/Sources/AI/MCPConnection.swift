import Foundation
import Network

final class MCPConnection {
    let id: UUID
    private let connection: NWConnection
    private let bearerToken: String
    private let toolRegistry: MCPToolRegistry
    private var sseSessionId: String?
    private var buffer = Data()

    var onClose: (() -> Void)?

    init(id: UUID, connection: NWConnection, bearerToken: String, toolRegistry: MCPToolRegistry) {
        self.id = id
        self.connection = connection
        self.bearerToken = bearerToken
        self.toolRegistry = toolRegistry
    }

    func start(queue: DispatchQueue) {
        connection.stateUpdateHandler = { [weak self] state in
            if case .failed = state { self?.close() }
            if case .cancelled = state { self?.onClose?() }
        }
        connection.start(queue: queue)
        receiveData()
    }

    func close() {
        connection.cancel()
    }

    private func receiveData() {
        connection.receive(minimumIncompleteLength: 1, maximumLength: 65536) { [weak self] data, _, isComplete, error in
            guard let self = self else { return }
            if let data = data { self.buffer.append(data) }
            if let error = error {
                NSLog("[MCPConnection] Receive error: \(error)")
                self.close()
                return
            }
            self.tryParseHTTPRequest()
            if isComplete {
                self.close()
            } else {
                self.receiveData()
            }
        }
    }

    private func tryParseHTTPRequest() {
        guard let headerEnd = buffer.range(of: Data("\r\n\r\n".utf8)) else { return }

        let headerData = buffer[buffer.startIndex..<headerEnd.lowerBound]
        guard let headerString = String(data: headerData, encoding: .utf8) else { return }

        let lines = headerString.components(separatedBy: "\r\n")
        guard let requestLine = lines.first else { return }
        let parts = requestLine.components(separatedBy: " ")
        guard parts.count >= 2 else { return }
        let method = parts[0]
        let path = parts[1]

        var headers: [String: String] = [:]
        for line in lines.dropFirst() {
            if let colonIdx = line.firstIndex(of: ":") {
                let key = line[line.startIndex..<colonIdx].trimmingCharacters(in: .whitespaces).lowercased()
                let val = line[line.index(after: colonIdx)...].trimmingCharacters(in: .whitespaces)
                headers[key] = val
            }
        }

        let bodyStart = headerEnd.upperBound
        let contentLength = Int(headers["content-length"] ?? "0") ?? 0
        let remainingData = buffer[bodyStart...]

        if remainingData.count < contentLength { return }

        let body = remainingData.prefix(contentLength)
        buffer = Data(remainingData.dropFirst(contentLength))

        if !validateAuth(headers: headers) {
            sendHTTPResponse(status: "401 Unauthorized", contentType: "text/plain", body: Data("Unauthorized".utf8))
            close()
            return
        }

        handleRequest(method: method, path: path, headers: headers, body: body)
    }

    private func validateAuth(headers: [String: String]) -> Bool {
        guard let auth = headers["authorization"] else { return false }
        return auth == "Bearer \(bearerToken)"
    }

    // MARK: - Request Routing

    private func handleRequest(method: String, path: String, headers: [String: String], body: Data) {
        if method == "GET" && path == "/sse" {
            handleSSE()
        } else if method == "POST" && path == "/message" {
            handleMessage(body: body)
        } else if method == "OPTIONS" {
            sendHTTPResponse(status: "200 OK", contentType: "text/plain", body: Data(), extraHeaders: corsHeaders())
        } else {
            sendHTTPResponse(status: "404 Not Found", contentType: "text/plain", body: Data("Not Found".utf8))
            close()
        }
    }

    // MARK: - SSE

    private func handleSSE() {
        sseSessionId = UUID().uuidString

        var response = "HTTP/1.1 200 OK\r\n"
        response += "Content-Type: text/event-stream\r\n"
        response += "Cache-Control: no-cache\r\n"
        response += "Connection: keep-alive\r\n"
        for (k, v) in corsHeaders() { response += "\(k): \(v)\r\n" }
        response += "\r\n"
        response += "event: endpoint\ndata: /message?sessionId=\(sseSessionId!)\n\n"

        connection.send(content: Data(response.utf8), completion: .contentProcessed { error in
            if let error = error {
                NSLog("[MCPConnection] SSE send error: \(error)")
            }
        })
    }

    // MARK: - JSON-RPC Message Handling

    private func handleMessage(body: Data) {
        guard let json = try? JSONSerialization.jsonObject(with: body) as? [String: Any],
              let method = json["method"] as? String else {
            sendJSONRPCError(id: nil, code: -32700, message: "Parse error")
            return
        }

        let requestId = json["id"]
        let params = json["params"] as? [String: Any] ?? [:]

        switch method {
        case "initialize":
            handleInitialize(id: requestId, params: params)
        case "notifications/initialized":
            sendHTTPResponse(status: "202 Accepted", contentType: "application/json", body: Data("{}".utf8))
        case "tools/list":
            handleToolsList(id: requestId)
        case "tools/call":
            handleToolsCall(id: requestId, params: params)
        default:
            sendJSONRPCError(id: requestId, code: -32601, message: "Method not found: \(method)")
        }
    }

    private func handleInitialize(id: Any?, params: [String: Any]) {
        let result: [String: Any] = [
            "protocolVersion": "2024-11-05",
            "capabilities": [
                "tools": ["listChanged": false]
            ],
            "serverInfo": [
                "name": "maho-browser",
                "version": "1.0.0"
            ]
        ]
        sendJSONRPCResult(id: id, result: result)
    }

    private func handleToolsList(id: Any?) {
        let tools = toolRegistry.listTools()
        sendJSONRPCResult(id: id, result: ["tools": tools])
    }

    private func handleToolsCall(id: Any?, params: [String: Any]) {
        guard let toolName = params["name"] as? String else {
            sendJSONRPCError(id: id, code: -32602, message: "Missing tool name")
            return
        }
        let arguments = params["arguments"] as? [String: Any] ?? [:]

        toolRegistry.callTool(name: toolName, arguments: arguments) { [weak self] (result: Result<Any, Error>) in
            switch result {
            case .success(let value):
                let content: [[String: Any]] = [
                    ["type": "text", "text": self?.jsonString(value) ?? ""]
                ]
                self?.sendJSONRPCResult(id: id, result: ["content": content])
            case .failure(let error):
                let content: [[String: Any]] = [
                    ["type": "text", "text": error.localizedDescription]
                ]
                self?.sendJSONRPCResult(id: id, result: ["content": content, "isError": true])
            }
        }
    }

    // MARK: - Response Helpers

    private func sendJSONRPCResult(id: Any?, result: Any) {
        var response: [String: Any] = ["jsonrpc": "2.0", "result": result]
        if let id = id { response["id"] = id }

        if sseSessionId != nil {
            sendSSEEvent(event: "message", data: response)
            sendHTTPResponse(status: "202 Accepted", contentType: "application/json", body: Data("{}".utf8))
        } else {
            guard let data = try? JSONSerialization.data(withJSONObject: response) else { return }
            sendHTTPResponse(status: "200 OK", contentType: "application/json", body: data)
        }
    }

    private func sendJSONRPCError(id: Any?, code: Int, message: String) {
        var response: [String: Any] = [
            "jsonrpc": "2.0",
            "error": ["code": code, "message": message]
        ]
        if let id = id { response["id"] = id }

        if sseSessionId != nil {
            sendSSEEvent(event: "message", data: response)
            sendHTTPResponse(status: "202 Accepted", contentType: "application/json", body: Data("{}".utf8))
        } else {
            guard let data = try? JSONSerialization.data(withJSONObject: response) else { return }
            sendHTTPResponse(status: "200 OK", contentType: "application/json", body: data)
        }
    }

    private func sendSSEEvent(event: String, data: Any) {
        guard let jsonData = try? JSONSerialization.data(withJSONObject: data),
              let jsonStr = String(data: jsonData, encoding: .utf8) else { return }
        let sseMessage = "event: \(event)\ndata: \(jsonStr)\n\n"
        connection.send(content: Data(sseMessage.utf8), completion: .contentProcessed { _ in })
    }

    private func sendHTTPResponse(status: String, contentType: String, body: Data, extraHeaders: [String: String] = [:]) {
        var response = "HTTP/1.1 \(status)\r\n"
        response += "Content-Type: \(contentType)\r\n"
        response += "Content-Length: \(body.count)\r\n"
        for (k, v) in corsHeaders() { response += "\(k): \(v)\r\n" }
        for (k, v) in extraHeaders { response += "\(k): \(v)\r\n" }
        response += "\r\n"

        var full = Data(response.utf8)
        full.append(body)
        connection.send(content: full, completion: .contentProcessed { _ in })
    }

    private func corsHeaders() -> [String: String] {
        return [
            "Access-Control-Allow-Origin": "*",
            "Access-Control-Allow-Headers": "Authorization, Content-Type",
            "Access-Control-Allow-Methods": "GET, POST, OPTIONS"
        ]
    }

    private func jsonString(_ value: Any) -> String {
        if let data = try? JSONSerialization.data(withJSONObject: value, options: .prettyPrinted),
           let str = String(data: data, encoding: .utf8) {
            return str
        }
        return "\(value)"
    }
}
