import type {ReactNode} from 'react';

import {TooltipProvider} from '@ui/tooltip';

import {ConfirmProvider} from '../components/ui/ConfirmDialog';
import type {
  Account,
  Attachment,
  Email,
  EmailDetail,
  EmailSummary,
  Folder,
} from '../types';

export function TestProviders({children}: {children: ReactNode}) {
  return (
    <TooltipProvider>
      <ConfirmProvider>{children}</ConfirmProvider>
    </TooltipProvider>
  );
}

const FIXED_DATE = '2026-01-15T09:30:00.000Z';

export function createMockAccount(overrides: Partial<Account> = {}): Account {
  return {
    id: 'acc-1',
    email: 'user@example.com',
    display_name: 'Test User',
    auth_type: 'password',
    imap_host: 'imap.example.com',
    imap_port: 993,
    imap_encryption: 'ssl',
    smtp_host: 'smtp.example.com',
    smtp_port: 587,
    smtp_encryption: 'starttls',
    username: 'user@example.com',
    created_at: FIXED_DATE,
    updated_at: FIXED_DATE,
    ...overrides,
  } as Account;
}

export function createMockFolder(overrides: Partial<Folder> = {}): Folder {
  return {
    id: 'folder-1',
    account_id: 'acc-1',
    name: 'Inbox',
    path: 'INBOX',
    folder_type: 'inbox',
    unread_count: 0,
    total_count: 0,
    ...overrides,
  } as Folder;
}

export function createMockEmail(overrides: Partial<EmailSummary> = {}):
    EmailSummary {
  return {
    id: 'email-1',
    account_id: 'acc-1',
    folder_id: 'folder-1',
    uid: 1,
    message_id: '<message-1@example.com>',
    subject: 'Test subject',
    from_address: 'sender@example.com',
    from_name: 'Sender',
    date: FIXED_DATE,
    snippet: 'Test snippet',
    is_read: false,
    is_starred: false,
    is_draft: false,
    has_attachments: false,
    ...overrides,
  };
}

export function createMockAttachment(overrides: Partial<Attachment> = {}):
    Attachment {
  return {
    id: 'attachment-1',
    part_id: '2',
    email_id: 'email-1',
    filename: 'document.pdf',
    mime_type: 'application/pdf',
    size: 1024,
    content_id: null,
    ...overrides,
  };
}

function createMockFullEmail(overrides: Partial<Email> = {}): Email {
  return {
    id: 'email-1',
    account_id: 'acc-1',
    folder_id: 'folder-1',
    uid: 1,
    message_id: '<message-1@example.com>',
    in_reply_to: null,
    subject: 'Test subject',
    from_address: 'sender@example.com',
    from_name: 'Sender',
    to_addresses: 'user@example.com',
    cc_addresses: null,
    bcc_addresses: null,
    date: FIXED_DATE,
    snippet: 'Test snippet',
    is_read: false,
    is_starred: false,
    is_draft: false,
    has_attachments: false,
    body_text: 'Test body',
    body_html: null,
    raw_size: 2048,
    created_at: FIXED_DATE,
    mdn_requested: null,
    ...overrides,
  };
}

export function createMockEmailDetail(
    overrides: {email?: Partial<Email>; attachments?: Attachment[]} = {}):
    EmailDetail {
  return {
    email: createMockFullEmail(overrides.email),
    attachments: overrides.attachments ?? [],
  };
}
