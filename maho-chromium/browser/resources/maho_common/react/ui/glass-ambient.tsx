// Ambient gradient backdrop reverted per design decision (the radial "blob"
// gradients faked depth behind glass and were removed).
//
// This file is intentionally kept as a no-op because it is still listed as a
// build source in maho_ai/BUILD.gn and maho_settings/BUILD.gn. Removing those
// GN entries (and this file) requires explicit approval per the repo GN-edit
// policy in AGENTS.md. Renders nothing.
export function GlassAmbient() {
  return null;
}
