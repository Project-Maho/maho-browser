// Copyright 2026 Maho Browser. All rights reserved.

#ifndef MAHO_BROWSER_AI_MAHO_ARTIFACT_HTML_SANITIZER_H_
#define MAHO_BROWSER_AI_MAHO_ARTIFACT_HTML_SANITIZER_H_

#include <cstddef>
#include <string>
#include <string_view>

namespace maho {

// Hard cap on sanitized output: 50 KiB. Output is truncated at a UTF-8 code
// point boundary so the total (including the marker) never exceeds this.
inline constexpr size_t kMaxSanitizedArtifactHtmlBytes = 50 * 1024;

// Appended to the output whenever truncation occurs. Reserved space is
// subtracted from the payload budget, so output.size() <=
// kMaxSanitizedArtifactHtmlBytes always holds.
inline constexpr char kSanitizedArtifactTruncationMarker[] =
    "\n[maho-sanitizer: output truncated at 50 KiB]";

// Deterministically sanitizes untrusted artifact HTML for serving from the
// chrome-untrusted://maho-ai-artifact-preview origin.
//
// This is a hand-written tag/attribute tokenizer (no regex) built around an
// ALLOWLIST: the only markup in the output is markup this function
// re-serializes itself from the allowlist. Every byte of attacker-controlled
// text and every attribute value is HTML-escaped on the way out, so unknown
// or obfuscated constructs can only be dropped, never passed through.
//
// Behavior summary:
//   * Tags: only a fixed allowlist of inert formatting/structure elements is
//     emitted. <script>, <svg> and <math> are removed together with their
//     entire subtrees (svg/math are script-execution bypass vectors, e.g.
//     <svg onload=...>). <iframe>/<object>/<embed>/<form>/<base>/<meta> and
//     every other non-allowlisted tag are dropped (their text children are
//     still sanitized and kept, except inside script/svg/math subtrees).
//   * Attributes: only a fixed allowlist (class/id/style/title/alt/...) is
//     emitted, which inherently strips every on* event handler and all
//     xmlns/xlink attributes. Values are always re-emitted double-quoted and
//     escaped.
//   * URLs: javascript: (including case/whitespace/NUL-obfuscated variants)
//     can never survive. img src is kept only for data: and blob: URLs;
//     every other src/href is removed except in-document "#fragment" hrefs.
//   * HTML comments, doctypes and processing instructions are stripped.
//   * Text is escaped (&, <, >; NUL bytes dropped). Well-formed entity
//     references are passed through verbatim — entities are decoded after
//     tokenization by the HTML parser, so they can never re-form markup.
//   * Output is capped at kMaxSanitizedArtifactHtmlBytes; on truncation the
//     kSanitizedArtifactTruncationMarker is appended.
//
// The function never executes, loads, or fetches anything and has no
// third-party dependencies.
std::string SanitizeArtifactHtml(std::string_view input);

} // namespace maho

#endif // MAHO_BROWSER_AI_MAHO_ARTIFACT_HTML_SANITIZER_H_
