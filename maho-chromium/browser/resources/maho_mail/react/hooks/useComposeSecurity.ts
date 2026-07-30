import { useState, useEffect, useCallback, type MutableRefObject } from "react";

export interface UseComposeSecurityParams {
  isOpen: boolean;
  prevIsOpenRef: MutableRefObject<boolean>;
}

export interface UseComposeSecurityReturn {
  pgpEncrypt: boolean;
  togglePgpEncrypt: (next: boolean) => void;
  smimeSign: boolean;
  toggleSmimeSign: (next: boolean) => void;
  smimeEncrypt: boolean;
  toggleSmimeEncrypt: (next: boolean) => void;
  requestReadReceipt: boolean;
  setRequestReadReceipt: (value: boolean) => void;
}

export function useComposeSecurity({
  isOpen,
  prevIsOpenRef,
}: UseComposeSecurityParams): UseComposeSecurityReturn {
  const [pgpEncrypt, setPgpEncrypt] = useState(false);
  const [smimeSign, setSmimeSign] = useState(false);
  const [smimeEncrypt, setSmimeEncrypt] = useState(false);
  const [requestReadReceipt, setRequestReadReceipt] = useState(false);

  // Reset all toggles on first-open transition (prevIsOpenRef false → true)
  useEffect(() => {
    if (!isOpen) return;

    const isFirstOpen = !prevIsOpenRef.current;
    if (!isFirstOpen) return;

    setPgpEncrypt(false);
    setSmimeSign(false);
    setSmimeEncrypt(false);
    setRequestReadReceipt(false);
  }, [isOpen, prevIsOpenRef]);

  const togglePgpEncrypt = useCallback((next: boolean) => {
    setPgpEncrypt(next);
    if (next) {
      setSmimeSign(false);
      setSmimeEncrypt(false);
    }
  }, []);

  const toggleSmimeSign = useCallback((next: boolean) => {
    setSmimeSign(next);
    if (next) {
      setPgpEncrypt(false);
      setSmimeEncrypt(false);
    }
  }, []);

  const toggleSmimeEncrypt = useCallback((next: boolean) => {
    setSmimeEncrypt(next);
    if (next) {
      setPgpEncrypt(false);
      setSmimeSign(false);
    }
  }, []);

  return {
    pgpEncrypt,
    togglePgpEncrypt,
    smimeSign,
    toggleSmimeSign,
    smimeEncrypt,
    toggleSmimeEncrypt,
    requestReadReceipt,
    setRequestReadReceipt,
  };
}
