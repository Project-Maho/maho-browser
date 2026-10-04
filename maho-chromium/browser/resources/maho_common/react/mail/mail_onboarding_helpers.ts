// Copyright 2026 Maho Browser. All rights reserved.

import {parseMailServerPort} from './mail_onboarding_api.js';
import type {MailServerEncryption} from './mail_onboarding_config.js';

export type CredentialPorts = {
  readonly imap: number;
  readonly smtp: number;
};

export type CredentialPortParseResult =
  | {readonly ok: true; readonly ports: CredentialPorts}
  | {readonly ok: false};

export type MailCredentialsRequestInput = {
  readonly mode: 'test' | 'add';
  readonly email: string;
  readonly displayName: string;
  readonly password: string;
  readonly imapHost: string;
  readonly imapPort: number;
  readonly imapEncryption: MailServerEncryption;
  readonly smtpHost: string;
  readonly smtpPort: number;
  readonly smtpEncryption: MailServerEncryption;
};

export function buildMailCredentialsRequestJson(input: MailCredentialsRequestInput): string {
  const username = input.email.trim();
  if (input.mode === 'test') {
    return JSON.stringify({
      imap_host: input.imapHost.trim(),
      imap_port: input.imapPort,
      imap_encryption: input.imapEncryption,
      username,
      auth_type: 'password',
      password: input.password,
    });
  }

  return JSON.stringify({
    email: username,
    display_name: input.displayName.trim() || username,
    auth_type: 'password',
    imap_host: input.imapHost.trim(),
    imap_port: input.imapPort,
    imap_encryption: input.imapEncryption,
    smtp_host: input.smtpHost.trim(),
    smtp_port: input.smtpPort,
    smtp_encryption: input.smtpEncryption,
    username,
    password: input.password,
  });
}

export function parseMailCredentialPorts(
  imapPortText: string,
  smtpPortText: string,
  includeSmtp: boolean,
): CredentialPortParseResult {
  const imap = parseMailServerPort(imapPortText);
  const smtp = parseMailServerPort(smtpPortText);
  if (!imap.ok || (includeSmtp && !smtp.ok)) return {ok: false};
  return {
    ok: true,
    ports: {
      imap: imap.value,
      smtp: smtp.ok ? smtp.value : 587,
    },
  };
}

export function deriveDisplayNameFromEmail(email: string): string {
  const localPart = email.split('@')[0] ?? '';
  return localPart.charAt(0).toUpperCase() + localPart.slice(1);
}
