import XCTest
@testable import Maho

final class BridgeAssetsTests: XCTestCase {

    private var tempDir: URL!

    override func setUp() {
        super.setUp()
        tempDir = FileManager.default.temporaryDirectory
            .appendingPathComponent("MahoAssetTest_\(UUID().uuidString)", isDirectory: true)
        try? FileManager.default.createDirectory(at: tempDir, withIntermediateDirectories: true)
    }

    override func tearDown() {
        try? FileManager.default.removeItem(at: tempDir)
        super.tearDown()
    }

    func testAssetManifestCodableRoundTrip() throws {
        let manifest = AssetManifest(
            schemaVersion: 1,
            bundleVersion: "1.2.3",
            buildTimestamp: 1700000000,
            files: [
                "web-ai/index.html": AssetManifest.AssetFileEntry(size: 1024, sha256: "abc123hash", modifiedTimestamp: 1700000000),
                "web-ai/ai-bundle.js": AssetManifest.AssetFileEntry(size: 20480, sha256: "def456hash", modifiedTimestamp: 1700000000)
            ]
        )

        let encoded = try JSONEncoder().encode(manifest)
        let decoded = try JSONDecoder().decode(AssetManifest.self, from: encoded)

        XCTAssertEqual(decoded.schemaVersion, 1)
        XCTAssertEqual(decoded.bundleVersion, "1.2.3")
        XCTAssertEqual(decoded.buildTimestamp, 1700000000)
        XCTAssertEqual(decoded.files.count, 2)
        XCTAssertEqual(decoded.files["web-ai/index.html"]?.size, 1024)
        XCTAssertEqual(decoded.files["web-ai/ai-bundle.js"]?.sha256, "def456hash")
    }

    func testAssetProvisioningFastPathSkipsWhenManifestIdentical() throws {
        let storagePath = tempDir.path

        // First provisioning pass: performs staging & atomic swap
        let firstResult = MahoBridge.shared.provisionAssets(storagePath: storagePath, bundle: .main)
        XCTAssertTrue(firstResult, "Initial asset provisioning should stage and swap assets")

        let assetsDir = tempDir.appendingPathComponent("assets", isDirectory: true)
        let manifestURL = assetsDir.appendingPathComponent("asset-manifest.json")
        XCTAssertTrue(FileManager.default.fileExists(atPath: manifestURL.path), "Manifest must exist after provisioning")

        // Second provisioning pass with same bundle/manifest: fast-path skips directory walk/copy
        let secondResult = MahoBridge.shared.provisionAssets(storagePath: storagePath, bundle: .main)
        XCTAssertFalse(secondResult, "Fast path should skip staging when manifest is identical")

        // Third pass with force = true: bypasses fast path
        let forcedResult = MahoBridge.shared.provisionAssets(storagePath: storagePath, bundle: .main, force: true)
        XCTAssertTrue(forcedResult, "Forced provisioning should re-stage assets")
    }

    func testAssetProvisioningDetectsManifestChange() throws {
        let storagePath = tempDir.path

        // Provision initial state
        _ = MahoBridge.shared.provisionAssets(storagePath: storagePath, bundle: .main)

        let assetsDir = tempDir.appendingPathComponent("assets", isDirectory: true)
        let manifestURL = assetsDir.appendingPathComponent("asset-manifest.json")

        // Simulate an older / different stored manifest version in sandbox
        let oldManifest = AssetManifest(
            schemaVersion: 1,
            bundleVersion: "0.0.1-old",
            buildTimestamp: 1000,
            files: [:]
        )
        let oldData = try JSONEncoder().encode(oldManifest)
        try oldData.write(to: manifestURL, options: .atomic)

        // Running provisionAssets now must detect change and re-provision
        let updatedResult = MahoBridge.shared.provisionAssets(storagePath: storagePath, bundle: .main)
        XCTAssertTrue(updatedResult, "Provisioning must update when stored manifest differs")

        // Verify stored manifest was updated
        let freshData = try Data(contentsOf: manifestURL)
        let freshManifest = try JSONDecoder().decode(AssetManifest.self, from: freshData)
        XCTAssertNotEqual(freshManifest.bundleVersion, "0.0.1-old")
    }

    func testProvisionedAssetURLRetrieval() throws {
        let storagePath = tempDir.path
        _ = MahoBridge.shared.provisionAssets(storagePath: storagePath, bundle: .main)

        let manifestFile = MahoBridge.shared.getProvisionedAssetURL(storagePath: storagePath, relativePath: "asset-manifest.json")
        XCTAssertNotNil(manifestFile)
        XCTAssertTrue(FileManager.default.fileExists(atPath: manifestFile!.path))

        let nonExistent = MahoBridge.shared.getProvisionedAssetURL(storagePath: storagePath, relativePath: "does_not_exist.txt")
        XCTAssertNil(nonExistent)
    }
}
