import XCTest
@testable import Maho

final class SqlCipherKeychainTests: XCTestCase {
    // Never the production service: that item encrypts the host app's real maho.db.
    private let service = "dev.maho.browser.storage.tests"

    override func setUp() {
        super.setUp()
        _ = SqlCipherKeychain.delete(service: service)
        _ = BYOKKeychain.delete(provider: "test-provider")
    }

    override func tearDown() {
        _ = SqlCipherKeychain.delete(service: service)
        _ = BYOKKeychain.delete(provider: "test-provider")
        super.tearDown()
    }

    func testFreshKeyStorageAndRetrieval() {
        let testKey = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"
        let setResult = SqlCipherKeychain.set(key: testKey, service: service)
        XCTAssertTrue(setResult, "SqlCipherKeychain.set must succeed on a fresh item")

        guard let data = SqlCipherKeychain.getData(service: service) else {
            XCTFail("SqlCipherKeychain.getData(service: service) must return stored data")
            return
        }
        let retrievedKey = String(data: data, encoding: .utf8)
        XCTAssertEqual(retrievedKey, testKey, "Retrieved key must match stored key")
    }

    func testUpdateExistingKey() {
        let key1 = "1111111111111111111111111111111111111111111111111111111111111111"
        let key2 = "2222222222222222222222222222222222222222222222222222222222222222"

        XCTAssertTrue(SqlCipherKeychain.set(key: key1, service: service), "Initial set must succeed")
        XCTAssertTrue(SqlCipherKeychain.set(key: key2, service: service), "Updating existing key must succeed")

        guard let data = SqlCipherKeychain.getData(service: service) else {
            XCTFail("SqlCipherKeychain.getData(service: service) must return stored data after update")
            return
        }
        let retrievedKey = String(data: data, encoding: .utf8)
        XCTAssertEqual(retrievedKey, key2, "Retrieved key must reflect updated value")
    }

    func testDeleteKey() {
        let testKey = "abcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcdefabcd"
        XCTAssertTrue(SqlCipherKeychain.set(key: testKey, service: service))
        XCTAssertTrue(SqlCipherKeychain.delete(service: service), "Delete must succeed")
        XCTAssertNil(SqlCipherKeychain.getData(service: service), "getData() after delete must return nil")
    }

    func testBYOKKeychainFreshStorageAndRetrieval() {
        let testKey = "sk-agent-test-key-12345"
        let setResult = BYOKKeychain.set(provider: "test-provider", key: testKey)
        XCTAssertTrue(setResult, "BYOKKeychain.set must succeed on fresh item")

        guard let data = BYOKKeychain.getData(provider: "test-provider") else {
            XCTFail("BYOKKeychain.getData must return stored data")
            return
        }
        let retrievedKey = String(data: data, encoding: .utf8)
        XCTAssertEqual(retrievedKey, testKey)
    }
}
