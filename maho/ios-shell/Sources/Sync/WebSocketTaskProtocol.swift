import Foundation

protocol WebSocketTaskProtocol: AnyObject {
    func send(_ message: URLSessionWebSocketTask.Message, completionHandler: @escaping @Sendable (Error?) -> Void)
    func receive(completionHandler: @escaping @Sendable (Result<URLSessionWebSocketTask.Message, Error>) -> Void)
    func cancel(with closeCode: URLSessionWebSocketTask.CloseCode, reason: Data?)
    func resume()
}

extension URLSessionWebSocketTask: WebSocketTaskProtocol {}

protocol WebSocketFactory {
    func createTask(with url: URL) -> WebSocketTaskProtocol
}

final class URLSessionWebSocketFactory: WebSocketFactory {
    private let session: URLSession

    init(session: URLSession) {
        self.session = session
    }

    func createTask(with url: URL) -> WebSocketTaskProtocol {
        return session.webSocketTask(with: url)
    }
}
