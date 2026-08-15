'use strict';

/** @typedef {'inbox'|'sent'|'drafts'|'starred'} FolderKind */

const FIXED_PUBLIC_PGP_CERT = [
    '-----BEGIN PGP PUBLIC KEY BLOCK-----',
    'Comment: Maho deterministic integration fixture (public only)',
    '',
    'bWFoby1tYWlsLWludGVncmF0aW9uLXB1YmxpYy1jZXJ0LXYx',
    '=test',
    '-----END PGP PUBLIC KEY BLOCK-----',
].join('\n');

const FIXED_SMIME_CERT = [
    '-----BEGIN CERTIFICATE-----',
    'bWFoby1tYWlsLWludGVncmF0aW9uLXNtaW1lLXB1YmxpYy1jZXJ0LXYx',
    '-----END CERTIFICATE-----',
].join('\n');

const FIXTURE = Object.freeze({
    credentials: Object.freeze({username: 'alice@example.test', password: 'fixture-password'}),
    certificates: Object.freeze({
        pgpPublic: FIXED_PUBLIC_PGP_CERT,
        smimePublic: FIXED_SMIME_CERT,
    }),
    accounts: Object.freeze([
        Object.freeze({id: 'account-alice', email: 'alice@example.test', displayName: 'Alice Example'}),
        Object.freeze({id: 'account-ops', email: 'ops@example.test', displayName: 'Ops Example'}),
    ]),
    folders: Object.freeze([
        Object.freeze({id: 'folder-inbox', accountId: 'account-alice', name: 'INBOX', kind: 'inbox', unread: 2}),
        Object.freeze({id: 'folder-sent', accountId: 'account-alice', name: 'Sent', kind: 'sent', unread: 0}),
        Object.freeze({id: 'folder-drafts', accountId: 'account-alice', name: 'Drafts', kind: 'drafts', unread: 0}),
        Object.freeze({id: 'folder-starred', accountId: 'account-alice', name: 'Starred', kind: 'starred', unread: 1}),
        Object.freeze({id: 'folder-ops-inbox', accountId: 'account-ops', name: 'INBOX', kind: 'inbox', unread: 1}),
    ]),
    messages: Object.freeze([
        Object.freeze({
            id: 'message-welcome', uid: 101, accountId: 'account-alice', folderId: 'folder-inbox',
            threadId: 'thread-welcome', from: 'Bob <bob@example.test>', to: 'alice@example.test',
            subject: 'Welcome to deterministic Mail', body: 'Fixed body alpha.', unread: true,
            starred: false, attachments: Object.freeze([]), cursor: 10,
        }),
        Object.freeze({
            id: 'message-attachment', uid: 102, accountId: 'account-alice', folderId: 'folder-inbox',
            threadId: 'thread-attachment', from: 'Carol <carol@example.test>', to: 'alice@example.test',
            subject: 'Quarterly attachment', body: 'Fixed body beta.', unread: true,
            starred: true, cursor: 11,
            attachments: Object.freeze([Object.freeze({
                id: 'attachment-report', partId: '2', filename: 'report.txt',
                mediaType: 'text/plain', contents: 'fixed attachment payload\n',
                capability: 'capability-report-once',
            })]),
        }),
        Object.freeze({
            id: 'message-muted', uid: 201, accountId: 'account-ops', folderId: 'folder-ops-inbox',
            threadId: 'thread-muted', from: 'Monitor <monitor@example.test>', to: 'ops@example.test',
            subject: 'Muted health report', body: 'Fixed body gamma.', unread: true,
            starred: false, attachments: Object.freeze([]), cursor: 20,
        }),
    ]),
    mutedThreadIds: Object.freeze(['thread-muted']),
});

function assertPublicOnlyFixture() {
    const serialized = JSON.stringify(FIXTURE);
    if (/PRIVATE KEY|private_key|secretKey/i.test(serialized)) {
        throw new Error('fixture contains private key material');
    }
}

module.exports = {FIXTURE, assertPublicOnlyFixture};
