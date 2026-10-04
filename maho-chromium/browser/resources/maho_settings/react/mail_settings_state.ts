// Copyright 2026 Maho Browser. All rights reserved.

export type MailSettingsState<T> =
    {readonly status: 'loading'}|
    {readonly status: 'ready'; readonly data: T}|
    {readonly status: 'empty'; readonly data: T}|
    {readonly status: 'unavailable'; readonly message: string}|
    {readonly status: 'error'; readonly message: string}|
    {readonly status: 'conflict'; readonly data: T; readonly message: string};

export function createMailSettingsContentState<T extends readonly unknown[]>(
    data: T): MailSettingsState<T> {
  return data.length === 0 ? {status: 'empty', data} : {status: 'ready', data};
}

export function classifyMailSettingsFailure(
    responseMessage: string, fallbackMessage: string):
    Extract<MailSettingsState<never>, {status: 'unavailable'|'error'}> {
  const message = responseMessage.trim();
  if (/\b(?:service\s+)?unavailable\b/i.test(message)) {
    return {status: 'unavailable', message};
  }
  return {status: 'error', message: message || fallbackMessage};
}
