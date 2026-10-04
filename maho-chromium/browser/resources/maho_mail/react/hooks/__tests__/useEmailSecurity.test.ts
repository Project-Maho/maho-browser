import { renderHook, act } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach } from "vitest";
import { useEmailSecurity } from "../useEmailSecurity";
import { createMockEmailDetail } from "../../test/mocks";
import * as api from "../../api";

vi.mock("../../api", () => ({
  decryptEmailPgp: vi.fn(),
  verifyEmailPgp: vi.fn(),
  decryptEmailSmime: vi.fn(),
  verifyEmailSmime: vi.fn(),
}));

describe("useEmailSecurity", () => {
  beforeEach(() => {
    vi.clearAllMocks();
  });

  it("detects PGP and S/MIME markers from the email body", () => {
    const emailDetail = createMockEmailDetail({
      email: {
        body_text:
          "-----BEGIN PGP MESSAGE-----\n-----BEGIN PGP SIGNED MESSAGE-----\napplication/pkcs7-mime\napplication/pkcs7-signature",
        body_html: "<p>application/x-pkcs7-mime application/x-pkcs7-signature</p>",
      },
    });

    const { result } = renderHook(() => useEmailSecurity({ emailDetail }));

    expect(result.current.isPgpEncrypted).toBe(true);
    expect(result.current.isPgpSigned).toBe(true);
    expect(result.current.isSmimeEncrypted).toBe(true);
    expect(result.current.isSmimeSigned).toBe(true);
  });

  it("decrypts and verifies PGP content using the email details", async () => {
    const emailDetail = createMockEmailDetail({
      email: {
        body_text:
          "Hello team\n\n-----BEGIN PGP SIGNATURE-----\nsigned-bytes",
      },
    });

    vi.mocked(api.decryptEmailPgp).mockResolvedValue("decrypted body");
    vi.mocked(api.verifyEmailPgp).mockResolvedValue({
      is_valid: true,
      signer_email: "sender@example.com",
      fingerprint: "fingerprint-123",
      error: null,
    });

    const { result } = renderHook(() => useEmailSecurity({ emailDetail }));

    await act(async () => {
      await result.current.handlePgpDecrypt();
    });

    expect(api.decryptEmailPgp).toHaveBeenCalledWith(
      emailDetail.email.account_id,
      emailDetail.email.body_text,
    );
    expect(result.current.pgpDecryptedText).toBe("decrypted body");
    expect(result.current.pgpDecrypting).toBe(false);
    expect(result.current.pgpDecryptError).toBeNull();

    await act(async () => {
      await result.current.handlePgpVerify();
    });

    expect(api.verifyEmailPgp).toHaveBeenCalledWith(
      emailDetail.email.from_address,
      "Hello team",
      "-----BEGIN PGP SIGNATURE-----\nsigned-bytes",
    );
    expect(result.current.pgpVerifyResult).toEqual({
      is_valid: true,
      signer_email: "sender@example.com",
      fingerprint: "fingerprint-123",
      error: null,
    });
  });

  it("handles S/MIME failures and resets security state", async () => {
    const emailDetail = createMockEmailDetail({
      email: {
        body_text:
          "body with enveloped-data and multipart/signed markers",
        body_html: "<div>application/x-pkcs7-mime</div>",
      },
    });

    vi.mocked(api.decryptEmailSmime).mockRejectedValue(new Error("decryption failed"));
    vi.mocked(api.verifyEmailSmime).mockRejectedValue(new Error("verification failed"));

    const { result } = renderHook(() => useEmailSecurity({ emailDetail }));

    await act(async () => {
      await result.current.handleSmimeDecrypt();
    });

    expect(result.current.smimeDecrypting).toBe(false);
    expect(result.current.smimeDecryptError).toBe("decryption failed");

    await act(async () => {
      await result.current.handleSmimeVerify();
    });

    expect(result.current.smimeVerifyResult).toEqual({
      valid: false,
      trusted: false,
      signer_email: null,
      signer_subject: "verification failed",
    });

    act(() => {
      result.current.resetSecurity();
    });

    expect(result.current.pgpDecryptedText).toBeNull();
    expect(result.current.pgpVerifyResult).toBeNull();
    expect(result.current.smimeDecryptedText).toBeNull();
    expect(result.current.smimeVerifyResult).toBeNull();
  });
});
