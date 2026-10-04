import { describe, it, expect } from "vitest";
import { isDangerousAttachment } from "../attachmentSafety";

describe("isDangerousAttachment", () => {
  describe("Windows executables and scripts (regression)", () => {
    it.each([
      ".exe",
      ".scr",
      ".lnk",
      ".bat",
      ".cmd",
      ".msi",
      ".ps1",
      ".vbs",
      ".com",
      ".pif",
      ".js",
      ".jse",
      ".vbe",
      ".wsf",
      ".ws",
      ".wsh",
      ".hta",
      ".cpl",
      ".msc",
      ".reg",
      ".scf",
      ".gadget",
      ".url",
    ])("flags %s as dangerous", (ext) => {
      expect(isDangerousAttachment({ filename: `payload${ext}` })).toBe(true);
    });
  });

  describe("macOS native packages and scripts", () => {
    it.each([
      ".app",
      ".dmg",
      ".pkg",
      ".command",
      ".workflow",
      ".scpt",
      ".applescript",
      ".terminal",
    ])("flags %s as dangerous", (ext) => {
      expect(isDangerousAttachment({ filename: `installer${ext}` })).toBe(true);
    });
  });

  describe("Linux native packages and scripts", () => {
    it.each([".deb", ".rpm", ".appimage", ".sh", ".bash", ".run"])(
      "flags %s as dangerous",
      (ext) => {
        expect(isDangerousAttachment({ filename: `pkg${ext}` })).toBe(true);
      },
    );
  });

  describe("Cross-platform runtimes", () => {
    it.each([".jar", ".py", ".pyw"])("flags %s as dangerous", (ext) => {
      expect(isDangerousAttachment({ filename: `tool${ext}` })).toBe(true);
    });
  });

  describe("HTML and disk-image content", () => {
    it.each([".html", ".htm", ".iso", ".img"])("flags %s as dangerous", (ext) => {
      expect(isDangerousAttachment({ filename: `payload${ext}` })).toBe(true);
    });
  });

  describe("MIME-type fallback (when filename is missing or generic)", () => {
    it.each([
      "text/html",
      "application/x-msdownload",
      "application/x-sh",
      "application/x-msdos-program",
      "application/x-apple-diskimage",
      "application/x-debian-package",
      "application/java-archive",
      "application/vnd.microsoft.portable-executable",
    ])("flags %s as dangerous", (mime) => {
      expect(
        isDangerousAttachment({ filename: "attachment", mimeType: mime }),
      ).toBe(true);
    });
  });

  describe("double-extension defense", () => {
    it("flags invoice.pdf.exe (last ext dangerous)", () => {
      expect(isDangerousAttachment({ filename: "invoice.pdf.exe" })).toBe(true);
    });

    it("flags report.exe.pdf (penultimate ext dangerous)", () => {
      expect(isDangerousAttachment({ filename: "report.exe.pdf" })).toBe(true);
    });

    it("flags malware.app.zip (penultimate ext dangerous)", () => {
      expect(isDangerousAttachment({ filename: "malware.app.zip" })).toBe(true);
    });

    it("flags screenshot.jpg.scr.png (deep nested ext)", () => {
      expect(
        isDangerousAttachment({ filename: "screenshot.jpg.scr.png" }),
      ).toBe(true);
    });
  });

  describe("whitespace and control character spoofing defense", () => {
    it("flags report.exe with trailing space", () => {
      expect(isDangerousAttachment({ filename: "report.exe " })).toBe(true);
    });

    it("flags report.exe with control characters and inner whitespace", () => {
      expect(isDangerousAttachment({ filename: "report.exe\r" })).toBe(true);
      expect(isDangerousAttachment({ filename: "report.exe\n" })).toBe(true);
      expect(isDangerousAttachment({ filename: "report.exe\t" })).toBe(true);
      expect(isDangerousAttachment({ filename: "report.exe .pdf" })).toBe(true);
    });
  });

  describe("macro-enabled and package formats", () => {
    it.each([
      "invoice.DOCM",
      "sheet.xlsm",
      "presentation.pptm",
      "template.dotm",
      "addin.xlam",
      "app.msix",
      "installer.appx",
      "troubleshoot.diagcab",
    ])("flags %s as dangerous", (filename) => {
      expect(isDangerousAttachment({ filename })).toBe(true);
    });
  });

  describe("safe filetypes (negative cases)", () => {
    it.each([
      "report.pdf",
      "photo.jpg",
      "photo.jpeg",
      "photo.png",
      "photo.gif",
      "doc.docx",
      "doc.doc",
      "data.csv",
      "data.xlsx",
      "notes.txt",
      "archive.zip",
      "presentation.pptx",
      "no_extension_filename",
    ])("does NOT flag %s", (filename) => {
      expect(isDangerousAttachment({ filename })).toBe(false);
    });
  });

  describe("case insensitivity", () => {
    it("flags .EXE (uppercase)", () => {
      expect(isDangerousAttachment({ filename: "MALWARE.EXE" })).toBe(true);
    });

    it("flags .DMG (uppercase)", () => {
      expect(isDangerousAttachment({ filename: "Installer.DMG" })).toBe(true);
    });

    it("flags TEXT/HTML mime (uppercase)", () => {
      expect(
        isDangerousAttachment({ filename: "x", mimeType: "TEXT/HTML" }),
      ).toBe(true);
    });
  });

  describe("edge cases", () => {
    it("safe when filename and mime are empty", () => {
      expect(isDangerousAttachment({ filename: "" })).toBe(false);
    });

    it("safe when only filename is given without extension", () => {
      expect(isDangerousAttachment({ filename: "readme" })).toBe(false);
    });

    it("safe for generic application/octet-stream without dangerous extension", () => {
      expect(
        isDangerousAttachment({
          filename: "data.bin",
          mimeType: "application/octet-stream",
        }),
      ).toBe(false);
    });
  });
});
