export type MailRoute =
  | {readonly kind: 'compose'}
  | {readonly kind: 'search'}
  | {readonly kind: 'folder'; readonly folder: 'Inbox' | 'Sent' | 'Drafts'}
  | {readonly kind: 'synthetic'; readonly view: 'starred' | 'snoozed' | 'unread'}
  | {readonly kind: 'none'};

export function parseMailRoute(search: string): MailRoute {
  const params = new URLSearchParams(search);
  const view = params.get('view');
  if (view === 'compose') return {kind: 'compose'};
  if (view === 'search') return {kind: 'search'};
  if (view === 'unread') return {kind: 'synthetic', view: 'unread'};
  if (view === 'snoozed') return {kind: 'synthetic', view: 'snoozed'};

  const folder = params.get('folder');
  if (folder === 'inbox') return {kind: 'folder', folder: 'Inbox'};
  if (folder === 'sent') return {kind: 'folder', folder: 'Sent'};
  if (folder === 'drafts') return {kind: 'folder', folder: 'Drafts'};
  if (folder === 'starred') return {kind: 'synthetic', view: 'starred'};
  return {kind: 'none'};
}
