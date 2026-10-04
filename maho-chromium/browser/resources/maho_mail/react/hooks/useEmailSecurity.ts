import { useState, useCallback, useMemo } from "react";
import * as api from "../api";
import type { EmailDetail } from "../types";
import type { PgpVerifyResult, SmimeVerifyResult } from "../types";

interface UseEmailSecurityOptions {
  emailDetail: EmailDetail | null;
}

interface UseEmailSecurityResult {
  isPgpEncrypted: boolean;
  isPgpSigned: boolean;
  pgpDecryptedText: string | null;
  pgpDecrypting: boolean;
  pgpDecryptError: string | null;
  pgpVerifyResult: PgpVerifyResult | null;
  handlePgpDecrypt: () => Promise<void>;
  handlePgpVerify: () => Promise<void>;
  isSmimeEncrypted: boolean;
  isSmimeSigned: boolean;
  smimeDecryptedText: string | null;
  smimeDecrypting: boolean;
  smimeDecryptError: string | null;
  smimeVerifyResult: SmimeVerifyResult | null;
  handleSmimeDecrypt: () => Promise<void>;
  handleSmimeVerify: () => Promise<void>;
  resetSecurity: () => void;
}

export function useEmailSecurity({
  emailDetail,
}: UseEmailSecurityOptions): UseEmailSecurityResult {
  const [pgpDecryptedText, setPgpDecryptedText] = useState<string | null>(null);
  const [pgpDecrypting, setPgpDecrypting] = useState(false);
  const [pgpDecryptError, setPgpDecryptError] = useState<string | null>(null);
  const [pgpVerifyResult, setPgpVerifyResult] = useState<PgpVerifyResult | null>(null);

  const [smimeDecryptedText, setSmimeDecryptedText] = useState<string | null>(null);
  const [smimeDecrypting, setSmimeDecrypting] = useState(false);
  const [smimeDecryptError, setSmimeDecryptError] = useState<string | null>(null);
  const [smimeVerifyResult, setSmimeVerifyResult] = useState<SmimeVerifyResult | null>(null);

  const email = emailDetail?.email ?? null;
  const bodyText = email?.body_text ?? "";
  const bodyHtmlStr = email?.body_html ?? "";

  const isPgpEncrypted = bodyText.includes("-----BEGIN PGP MESSAGE-----");
  const isPgpSigned = bodyText.includes("-----BEGIN PGP SIGNED MESSAGE-----");

  const isSmimeEncrypted =
    bodyText.includes("application/pkcs7-mime") ||
    bodyText.includes("application/x-pkcs7-mime") ||
    bodyHtmlStr.includes("application/pkcs7-mime") ||
    bodyHtmlStr.includes("application/x-pkcs7-mime") ||
    bodyText.includes("enveloped-data");
  const isSmimeSigned =
    bodyText.includes("application/pkcs7-signature") ||
    bodyText.includes("application/x-pkcs7-signature") ||
    bodyHtmlStr.includes("application/pkcs7-signature") ||
    bodyHtmlStr.includes("application/x-pkcs7-signature") ||
    bodyText.includes("signed-data") ||
    bodyText.includes("multipart/signed");

  const handlePgpDecrypt = useCallback(async () => {
    if (!email) return;
    try {
      setPgpDecrypting(true);
      setPgpDecryptError(null);
      const decrypted = await api.decryptEmailPgp(email.account_id, bodyText);
      setPgpDecryptedText(decrypted);
    } catch (err) {
      setPgpDecryptError(err instanceof Error ? err.message : "Decryption failed");
    } finally {
      setPgpDecrypting(false);
    }
  }, [email, bodyText]);

  const handlePgpVerify = useCallback(async () => {
    if (!email) return;
    try {
      let message: string;
      let signature: string;

      if (bodyText.includes("-----BEGIN PGP SIGNED MESSAGE-----")) {
        message = "";
        signature = bodyText;
      } else {
        const sigStart = bodyText.indexOf("-----BEGIN PGP SIGNATURE-----");
        if (sigStart !== -1) {
          message = bodyText.slice(0, sigStart).trimEnd();
          signature = bodyText.slice(sigStart);
        } else {
          message = "";
          signature = bodyText;
        }
      }

      const result = await api.verifyEmailPgp(email.from_address, message, signature);
      setPgpVerifyResult(result);
    } catch (err) {
      setPgpVerifyResult({
        is_valid: false,
        signer_email: email.from_address,
        fingerprint: null,
        error: err instanceof Error ? err.message : "Verification failed",
      });
    }
  }, [email, bodyText]);

  const handleSmimeDecrypt = useCallback(async () => {
    if (!email) return;
    try {
      setSmimeDecrypting(true);
      setSmimeDecryptError(null);
      const decrypted = await api.decryptEmailSmime(email.account_id, bodyText);
      setSmimeDecryptedText(decrypted);
    } catch (err) {
      setSmimeDecryptError(err instanceof Error ? err.message : "S/MIME decryption failed");
    } finally {
      setSmimeDecrypting(false);
    }
  }, [email, bodyText]);

  const handleSmimeVerify = useCallback(async () => {
    if (!email) return;
    try {
      const result = await api.verifyEmailSmime(bodyText);
      setSmimeVerifyResult(result);
    } catch (err) {
      setSmimeVerifyResult({
        valid: false,
        trusted: false,
        signer_email: null,
        signer_subject: err instanceof Error ? err.message : "Verification failed",
      });
    }
  }, [email, bodyText]);

  const resetSecurity = useCallback(() => {
    setPgpDecryptedText(null);
    setPgpDecrypting(false);
    setPgpDecryptError(null);
    setPgpVerifyResult(null);
    setSmimeDecryptedText(null);
    setSmimeDecrypting(false);
    setSmimeDecryptError(null);
    setSmimeVerifyResult(null);
  }, []);

  return useMemo(
    () => ({
      isPgpEncrypted,
      isPgpSigned,
      pgpDecryptedText,
      pgpDecrypting,
      pgpDecryptError,
      pgpVerifyResult,
      handlePgpDecrypt,
      handlePgpVerify,
      isSmimeEncrypted,
      isSmimeSigned,
      smimeDecryptedText,
      smimeDecrypting,
      smimeDecryptError,
      smimeVerifyResult,
      handleSmimeDecrypt,
      handleSmimeVerify,
      resetSecurity,
    }),
    [
      isPgpEncrypted,
      isPgpSigned,
      pgpDecryptedText,
      pgpDecrypting,
      pgpDecryptError,
      pgpVerifyResult,
      handlePgpDecrypt,
      handlePgpVerify,
      isSmimeEncrypted,
      isSmimeSigned,
      smimeDecryptedText,
      smimeDecrypting,
      smimeDecryptError,
      smimeVerifyResult,
      handleSmimeDecrypt,
      handleSmimeVerify,
      resetSecurity,
    ],
  );
}
