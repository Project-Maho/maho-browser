import XCTest
@testable import Maho

final class BridgeSpacesTests: XCTestCase {

    private let encoder = JSONEncoder()

    // MARK: - createSpace sends correct ShellEvent

    func testCreateSpaceEventEncoding() throws {
        let color = SpaceColor(hue: 0.55, saturation: 0.75, brightness: 0.85)
        let event = ShellEvent.createSpace(name: "Work", color: color, profileId: "profile-1")
        let data = try encoder.encode(event)
        let json = try XCTUnwrap(JSONSerialization.jsonObject(with: data) as? [String: Any])

        XCTAssertEqual(json["kind"] as? String, "create_space")
        XCTAssertEqual(json["name"] as? String, "Work")
        XCTAssertEqual(json["profile_id"] as? String, "profile-1")

        let colorDict = try XCTUnwrap(json["color"] as? [String: Double])
        XCTAssertEqual(try XCTUnwrap(colorDict["hue"]), 0.55, accuracy: 0.001)
        XCTAssertEqual(try XCTUnwrap(colorDict["saturation"]), 0.75, accuracy: 0.001)
        XCTAssertEqual(try XCTUnwrap(colorDict["brightness"]), 0.85, accuracy: 0.001)
    }

    // MARK: - SpaceColor encoding

    func testSpaceColorRoundTrip() throws {
        let original = SpaceColor(hue: 0.2, saturation: 0.4, brightness: 0.6)
        let data = try encoder.encode(original)
        let decoded = try JSONDecoder().decode(SpaceColor.self, from: data)

        XCTAssertEqual(decoded.hue, original.hue, accuracy: 0.001)
        XCTAssertEqual(decoded.saturation, original.saturation, accuracy: 0.001)
        XCTAssertEqual(decoded.brightness, original.brightness, accuracy: 0.001)
    }

    // MARK: - FolderViewModel decoding

    func testFolderViewModelDecoding() throws {
        let json = """
        {
            "id": "folder-1",
            "name": "Reading List",
            "tabCount": 5,
            "isExpanded": true,
            "tabIds": ["tab-1", "tab-2"],
            "isPinned": false,
            "parentFolderId": null
        }
        """
        let data = try XCTUnwrap(json.data(using: .utf8))
        let model = try JSONDecoder().decode(FolderViewModel.self, from: data)

        XCTAssertEqual(model.id, "folder-1")
        XCTAssertEqual(model.name, "Reading List")
        XCTAssertEqual(model.tabCount, 5)
        XCTAssertTrue(model.isExpanded)
        XCTAssertEqual(model.tabIds, ["tab-1", "tab-2"])
        XCTAssertFalse(model.isPinned)
        XCTAssertNil(model.parentFolderId)
    }

    func testFolderViewModelRoundTrip() throws {
        let original = FolderViewModel(
            id: "folder-2",
            name: "Projects",
            tabCount: 3,
            isExpanded: false,
            tabIds: ["tab-3"],
            isPinned: true,
            parentFolderId: "folder-parent"
        )

        let data = try encoder.encode(original)
        let decoded = try JSONDecoder().decode(FolderViewModel.self, from: data)

        XCTAssertEqual(decoded.id, original.id)
        XCTAssertEqual(decoded.name, original.name)
        XCTAssertEqual(decoded.tabCount, original.tabCount)
        XCTAssertEqual(decoded.isExpanded, original.isExpanded)
        XCTAssertEqual(decoded.tabIds, original.tabIds)
        XCTAssertEqual(decoded.isPinned, original.isPinned)
        XCTAssertEqual(decoded.parentFolderId, original.parentFolderId)
    }
}
