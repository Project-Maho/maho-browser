import XCTest
@testable import Maho

/// Pins the offline half of the filter-list contract: which fetch errors mean
/// "never reached the server", and the status code core expects for them.
final class FilterListUpdaterConnectivityTests: XCTestCase {

    func testConnectivityErrorsAreRecognized() {
        let connectivityCodes: [URLError.Code] = [
            .notConnectedToInternet, .networkConnectionLost, .timedOut,
            .cannotFindHost, .cannotConnectToHost, .dnsLookupFailed,
            .internationalRoamingOff, .callIsActive, .dataNotAllowed,
            .cancelled
        ]
        for code in connectivityCodes {
            XCTAssertTrue(
                FilterListUpdater.isConnectivityError(URLError(code)),
                "\(code) means the request never reached the server"
            )
        }
    }

    func testOtherErrorsEngageTheBackoff() {
        XCTAssertFalse(FilterListUpdater.isConnectivityError(URLError(.badServerResponse)))
        XCTAssertFalse(FilterListUpdater.isConnectivityError(URLError(.secureConnectionFailed)))
        XCTAssertFalse(FilterListUpdater.isConnectivityError(NSError(domain: "dev.maho.test", code: 1)))
    }

    func testTransportFailureStatusMatchesCore() {
        XCTAssertEqual(
            FilterListUpdater.transportFailureStatus, 1,
            "core's TRANSPORT_FAILURE_STATUS; any other value escalates offline retries"
        )
        XCTAssertEqual(FilterListUpdater.unreachableStatus, 0)
    }
}
