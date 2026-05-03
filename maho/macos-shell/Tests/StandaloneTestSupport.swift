import Foundation

func assertEqual<T: Equatable>(_ a: T, _ b: T, file: String = #file, line: Int = #line) {
    if a != b {
        print("FAIL [\(file):\(line)]: \(a) != \(b)")
        exit(1)
    }
}

func assertTrue(_ condition: Bool, _ message: String = "", file: String = #file, line: Int = #line) {
    if !condition {
        print("FAIL [\(file):\(line)]: \(message)")
        exit(1)
    }
}

func assertFalse(_ condition: Bool, _ message: String = "", file: String = #file, line: Int = #line) {
    if condition {
        print("FAIL [\(file):\(line)]: \(message)")
        exit(1)
    }
}
