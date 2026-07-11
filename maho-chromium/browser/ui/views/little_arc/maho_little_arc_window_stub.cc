// maho_little_arc_window_stub.cc — no-op stub for platforms that don't ship Little Arc.
// Follows the established stub pattern (see maho_toast_vibrancy_stub.cc, maho_command_overlay_vibrancy_stub.cc).
#include "maho/browser/ui/views/little_arc/maho_little_arc_window.h"

// static
void MahoLittleArcWindow::Open(Browser* /*browser*/, const GURL& /*url*/) {
  // DEFERRED on Windows and Linux for v1.
  // See .omo/plans/cross-platform-completion-plan-2026-07-10.md B.2.
}

// static
void MahoLittleArcWindow::PromoteToTab(Browser* /*browser*/) {
  // DEFERRED on Windows and Linux for v1.
}
