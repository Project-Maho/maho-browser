import XCTest
@testable import Maho

final class AgenticBrowsingScenariosTests: XCTestCase {

    override func setUpWithError() throws {
        continueAfterFailure = false
    }

    func testScenario1_ExtractPageInteractiveSnapshot() throws {
        let fakeHtmlResponse = """
        {
            "ok": true,
            "result": {
                "url": "https://service.example.com/checkout",
                "title": "Checkout Order",
                "viewport": {"width": 390, "height": 844, "scrollX": 0, "scrollY": 0},
                "elements": [
                    {
                        "id": 1,
                        "tag": "input",
                        "type": "text",
                        "label": "Shipping Address",
                        "selector": "#shipping-addr",
                        "bounds": {"x": 16.0, "y": 120.0, "width": 358.0, "height": 48.0}
                    },
                    {
                        "id": 2,
                        "tag": "input",
                        "type": "tel",
                        "label": "Phone Number",
                        "selector": "#phone-number",
                        "bounds": {"x": 16.0, "y": 180.0, "width": 358.0, "height": 48.0}
                    },
                    {
                        "id": 3,
                        "tag": "button",
                        "type": "submit",
                        "label": "Pay Now",
                        "selector": "#pay-btn",
                        "bounds": {"x": 16.0, "y": 300.0, "width": 358.0, "height": 52.0}
                    }
                ]
            }
        }
        """

        let result = AgenticBrowsingDOM.invoke(
            name: "get_page_elements",
            args: [:]
        ) { script in
            XCTAssertTrue(script.contains("querySelectorAll"))
            return .success(fakeHtmlResponse)
        }

        XCTAssertEqual(result["ok"] as? Bool, true)
        let payload = try XCTUnwrap(result["result"] as? [String: Any])
        XCTAssertEqual(payload["title"] as? String, "Checkout Order")
        let elements = try XCTUnwrap(payload["elements"] as? [[String: Any]])
        XCTAssertEqual(elements.count, 3)
        XCTAssertEqual(elements[0]["label"] as? String, "Shipping Address")
        XCTAssertEqual(elements[2]["tag"] as? String, "button")
    }

    func testScenario2_FormFillingWithSyntheticEvents() throws {
        let fakeFillResponse = """
        {
            "ok": true,
            "result": {
                "id": null,
                "selector": "#shipping-addr",
                "filled": true,
                "length": 25
            }
        }
        """

        let result = AgenticBrowsingDOM.invoke(
            name: "fill_input",
            args: ["selector": "#shipping-addr", "text": "123 Gangnam-daero, Seoul"]
        ) { script in
            XCTAssertTrue(script.contains("InputEvent('input'"))
            XCTAssertTrue(script.contains("Event('change'"))
            XCTAssertTrue(script.contains("123 Gangnam-daero, Seoul"))
            return .success(fakeFillResponse)
        }

        XCTAssertEqual(result["ok"] as? Bool, true)
        let payload = try XCTUnwrap(result["result"] as? [String: Any])
        XCTAssertEqual(payload["filled"] as? Bool, true)
        XCTAssertEqual(payload["length"] as? Int, 25)
        XCTAssertNil(payload["text"], "Typed text must not echo back in tool result")
    }

    func testScenario3_TouchClickAndPageScrollSequence() throws {
        let fakeClickResponse = """
        {"ok": true, "result": {"id": 3, "selector": null, "clicked": true}}
        """

        let clickResult = AgenticBrowsingDOM.invoke(
            name: "click_element",
            args: ["id": 3]
        ) { script in
            XCTAssertTrue(script.contains("refAttr"))
            XCTAssertTrue(script.contains("touchstart"))
            XCTAssertTrue(script.contains("click"))
            return .success(fakeClickResponse)
        }

        XCTAssertEqual(clickResult["ok"] as? Bool, true)
        let clickPayload = try XCTUnwrap(clickResult["result"] as? [String: Any])
        XCTAssertEqual(clickPayload["clicked"] as? Bool, true)
        XCTAssertEqual(clickPayload["id"] as? Int, 3)

        let fakeScrollResponse = """
        {
            "ok": true,
            "result": {
                "direction": "down",
                "amount": 400.0,
                "from": {"x": 0.0, "y": 0.0},
                "target": {"x": 0.0, "y": 400.0}
            }
        }
        """

        let scrollResult = AgenticBrowsingDOM.invoke(
            name: "scroll_page",
            args: ["direction": "down", "amount": 400]
        ) { script in
            XCTAssertTrue(script.contains("scrollBy"))
            XCTAssertTrue(script.contains("400"))
            return .success(fakeScrollResponse)
        }

        XCTAssertEqual(scrollResult["ok"] as? Bool, true)
        let scrollPayload = try XCTUnwrap(scrollResult["result"] as? [String: Any])
        XCTAssertEqual(scrollPayload["direction"] as? String, "down")
        let target = try XCTUnwrap(scrollPayload["target"] as? [String: Any])
        XCTAssertEqual(target["y"] as? Double, 400.0)
    }
}
