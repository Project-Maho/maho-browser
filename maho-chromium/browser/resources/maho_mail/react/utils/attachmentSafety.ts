/**
 * Attachment safety classification.
 *
 * Returns true when an attachment's filename or MIME type matches a known
 * code-execution surface that should require user confirmation before the
 * OS handler launches it. The list covers macOS, Linux, and Windows native
 * executable / installer formats plus the common scripting families that
 * email-borne malware uses as droppers.
 *
 * Defense-in-depth notes:
 *  - Both filename extension AND MIME type are consulted; either match
 *    flags the attachment.
 *  - Multi-extension filenames (e.g. `invoice.pdf.exe`, `report.exe.pdf`)
 *    are flagged if ANY dotted segment is dangerous, not only the last.
 *  - Matching is case-insensitive.
 *  - The list is conservative; users can still explicitly confirm to open.
 */

const DANGEROUS_EXTENSIONS: ReadonlySet<string> = new Set([
  // Windows native executables and installers
  ".exe",
  ".scr",
  ".com",
  ".pif",
  ".msi",
  ".bat",
  ".cmd",
  ".lnk",
  ".gadget",
  ".url",
  ".cpl",
  ".msc",
  ".scf",
  // Windows script hosts
  ".ps1",
  ".ps1xml",
  ".psc1",
  ".vbs",
  ".vbe",
  ".js",
  ".jse",
  ".wsf",
  ".ws",
  ".wsh",
  ".hta",
  ".reg",
  // macOS native packages and scripts
  ".app",
  ".dmg",
  ".pkg",
  ".command",
  ".workflow",
  ".scpt",
  ".applescript",
  ".terminal",
  // Linux native packages and scripts
  ".deb",
  ".rpm",
  ".appimage",
  ".sh",
  ".bash",
  ".run",
  // Cross-platform runtimes
  ".jar",
  ".py",
  ".pyw",
  // HTML-style content (can host phishing pages / inline scripts)
  ".html",
  ".htm",
  // Disk images (often carry signed installers)
  ".iso",
  ".img",
  // Office macro formats
  ".docm",
  ".dotm",
  ".xlsm",
  ".xltm",
  ".xlam",
  ".pptm",
  ".potm",
  ".ppam",
  ".ppsm",
  // Windows packages and diagnostics
  ".msix",
  ".msixbundle",
  ".appx",
  ".appxbundle",
  ".diagcab",
]);

const DANGEROUS_MIME_TYPES: ReadonlySet<string> = new Set([
  "text/html",
  "application/x-msdownload",
  "application/x-sh",
  "application/x-msdos-program",
  "application/x-apple-diskimage",
  "application/x-debian-package",
  "application/java-archive",
  "application/vnd.microsoft.portable-executable",
]);

export interface AttachmentSafetyInput {
  filename: string;
  mimeType?: string;
}

/**
 * Extract every dotted segment in a filename as a lowercase extension token
 * (including the leading `.`). For `report.pdf.exe` this returns
 * `[".pdf", ".exe"]` so the double-extension defense can match the dangerous
 * segment regardless of position.
 */
function extractExtensions(filename: string): string[] {
  if (!filename) return [];
  const parts = filename.split(".").slice(1); // drop the base name
  return parts
    .map((part) => {
      const cleaned = part.replace(/^[\s\x00-\x1f\x7f-\x9f]+|[\s\x00-\x1f\x7f-\x9f]+$/g, "").toLowerCase();
      return cleaned ? `.${cleaned}` : "";
    })
    .filter(Boolean);
}

export function isDangerousAttachment(input: AttachmentSafetyInput): boolean {
  const filename = input.filename ?? "";
  const mime = (input.mimeType ?? "").toLowerCase();

  const extensions = extractExtensions(filename);
  for (const ext of extensions) {
    if (DANGEROUS_EXTENSIONS.has(ext)) return true;
  }

  if (mime && DANGEROUS_MIME_TYPES.has(mime)) return true;

  return false;
}
