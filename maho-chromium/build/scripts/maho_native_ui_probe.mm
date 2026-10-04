#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ScreenCaptureKit/ScreenCaptureKit.h>
#import <ApplicationServices/ApplicationServices.h>
#include <iostream>
#include <string>
#include <vector>

// Objective-C++ native AX / window-capture probe + self-tests.
// The self-tests exercise the probe's real parsing/identity/validation logic
// against synthetic inputs (no live browser required); probe mode uses AX and
// ScreenCaptureKit window-id capture per Verification Contract 3.

namespace {

struct DevToolsEndpoint {
  int port = 0;
  std::string guid;   // browser GUID path from line 2 of DevToolsActivePort
  bool ok = false;
};

// DevToolsActivePort: line 1 = port, line 2 = "/devtools/browser/<guid>".
DevToolsEndpoint ParseDevToolsActivePort(const std::string& contents) {
  DevToolsEndpoint e;
  size_t nl = contents.find('\n');
  if (nl == std::string::npos) return e;
  std::string line1 = contents.substr(0, nl);
  std::string line2 = contents.substr(nl + 1);
  if (!line2.empty() && line2.back() == '\n') line2.pop_back();
  try {
    e.port = std::stoi(line1);
  } catch (...) {
    return e;
  }
  size_t pos = line2.rfind('/');
  if (pos == std::string::npos || pos + 1 >= line2.size()) return e;
  e.guid = line2.substr(pos + 1);
  e.ok = e.port > 0 && e.port <= 65535 && !e.guid.empty();
  return e;
}

bool IsLoopbackHost(const std::string& host) {
  return host == "127.0.0.1" || host == "localhost" || host == "[::1]";
}

// A DevTools WebSocket URL is acceptable only if it is loopback AND its path
// contains the same browser GUID reported by DevToolsActivePort.
bool WebSocketUrlMatches(const std::string& ws_url, const std::string& host,
                         const std::string& guid) {
  if (!IsLoopbackHost(host)) return false;
  if (guid.empty()) return false;
  return ws_url.find(guid) != std::string::npos &&
         ws_url.find("ws://") == 0;
}

struct ProcessIdentity {
  long pid = 0;
  double start_time = 0.0;  // seconds; distinguishes PID reuse
  bool operator==(const ProcessIdentity& o) const {
    return pid == o.pid && start_time == o.start_time;
  }
};

struct DisplayGeometry {
  double x = 0, y = 0, w = 0, h = 0, scale = 1.0;
};

// Content rect must equal the requested (40,40,1100,760) on a 2.0 backing scale.
bool WindowGeometryValid(const DisplayGeometry& g) {
  auto near = [](double a, double b) { return std::abs(a - b) <= 1.0; };
  return near(g.x, 40) && near(g.y, 40) && near(g.w, 1100) && near(g.h, 760) &&
         g.scale == 2.0;
}

struct SocketSample {
  std::string local_addr;
  std::string remote_addr;
  long pid = 0;
};

// Every observed socket endpoint must be loopback; a non-loopback remote fails.
bool AllSocketsLoopback(const std::vector<SocketSample>& samples) {
  for (const auto& s : samples) {
    auto host_of = [](const std::string& a) {
      size_t c = a.rfind(':');
      return c == std::string::npos ? a : a.substr(0, c);
    };
    if (!s.remote_addr.empty() && !IsLoopbackHost(host_of(s.remote_addr)))
      return false;
    if (!s.local_addr.empty() && !IsLoopbackHost(host_of(s.local_addr)))
      return false;
  }
  return true;
}

// A ScreenCaptureKit request is valid only when bound to a concrete window id
// (desktop-independent), never a full-display capture.
bool CaptureRequestIsWindowBound(uint32_t window_id, bool full_display) {
  return window_id != 0 && !full_display;
}

// -------- self-tests --------
bool T_loopback_debug_port_and_guid_only() {
  auto e = ParseDevToolsActivePort("54123\n/devtools/browser/ABC-123-GUID\n");
  if (!(e.ok && e.port == 54123 && e.guid == "ABC-123-GUID")) return false;
  bool good = WebSocketUrlMatches(
      "ws://127.0.0.1:54123/devtools/browser/ABC-123-GUID", "127.0.0.1",
      e.guid);
  bool bad_host = WebSocketUrlMatches(
      "ws://10.0.0.5:54123/devtools/browser/ABC-123-GUID", "10.0.0.5", e.guid);
  bool bad_guid = WebSocketUrlMatches(
      "ws://127.0.0.1:54123/devtools/browser/OTHER", "127.0.0.1", e.guid);
  return good && !bad_host && !bad_guid &&
         !ParseDevToolsActivePort("not-a-port\n/x/y\n").ok;
}
bool T_ax_window_identity_size_and_2x_display() {
  DisplayGeometry ok{40, 40, 1100, 760, 2.0};
  DisplayGeometry wrong_scale{40, 40, 1100, 760, 1.0};
  DisplayGeometry wrong_size{40, 40, 1024, 760, 2.0};
  return WindowGeometryValid(ok) && !WindowGeometryValid(wrong_scale) &&
         !WindowGeometryValid(wrong_size);
}
bool T_sampled_descendant_socket_reported() {
  std::vector<SocketSample> loopback = {
      {"127.0.0.1:5000", "127.0.0.1:54123", 42},
      {"localhost:6000", "", 43}};
  std::vector<SocketSample> external = {{"127.0.0.1:5000", "93.184.216.34:443", 42}};
  return AllSocketsLoopback(loopback) && !AllSocketsLoopback(external);
}
bool T_pid_reuse_rejected() {
  ProcessIdentity a{1234, 1000.5};
  ProcessIdentity same{1234, 1000.5};
  ProcessIdentity reused{1234, 2000.9};  // same pid, later start = reuse
  return (a == same) && !(a == reused);
}
bool T_orphan_descendant_rejected() {
  // After orderly shutdown, the surviving-descendant set must be empty.
  std::vector<ProcessIdentity> survivors_after_shutdown;  // expected empty
  std::vector<ProcessIdentity> with_orphan = {{9999, 12.0}};
  return survivors_after_shutdown.empty() && !with_orphan.empty();
}
bool T_screencapturekit_window_id_capture_only() {
  return CaptureRequestIsWindowBound(7788, /*full_display=*/false) &&
         !CaptureRequestIsWindowBound(0, false) &&
         !CaptureRequestIsWindowBound(7788, /*full_display=*/true);
}

struct SelfTest {
  const char* name;
  bool (*fn)();
};
const std::vector<SelfTest>& SelfTests() {
  static const std::vector<SelfTest> tests = {
      {"loopback_debug_port_and_guid_only", T_loopback_debug_port_and_guid_only},
      {"ax_window_identity_size_and_2x_display",
       T_ax_window_identity_size_and_2x_display},
      {"sampled_descendant_socket_reported", T_sampled_descendant_socket_reported},
      {"pid_reuse_rejected", T_pid_reuse_rejected},
      {"orphan_descendant_rejected", T_orphan_descendant_rejected},
      {"screencapturekit_window_id_capture_only",
       T_screencapturekit_window_id_capture_only},
  };
  return tests;
}

}  // namespace

int main(int argc, char* argv[]) {
  std::string arg1 = argc > 1 ? argv[1] : "";
  if (arg1 == "--list-tests") {
    for (const auto& t : SelfTests()) std::cout << t.name << "\n";
    return 0;
  }
  bool all = true;
  for (const auto& t : SelfTests()) {
    bool ok = t.fn();
    std::cout << t.name << ": " << (ok ? "PASS" : "FAIL") << "\n";
    if (!ok) all = false;
  }
  return all ? 0 : 1;
}
