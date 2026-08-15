'use strict';

const net = require('node:net');
const {EventEmitter, once} = require('node:events');
const {FIXTURE} = require('./fixed-mail-fixture.js');

class ReplyBarrier {
    constructor(name) {
        this.name = name;
        this.blocked = false;
        this.waiters = [];
    }

    block() { this.blocked = true; }

    release() {
        this.blocked = false;
        const waiters = this.waiters.splice(0);
        for (const resolve of waiters) resolve();
    }

    async pass() {
        if (!this.blocked) return;
        await new Promise((resolve) => this.waiters.push(resolve));
    }
}

class ProtocolTranscript extends EventEmitter {
    constructor() {
        super();
        this.entries = [];
        this.nextSequence = 1;
    }

    push(protocol, direction, line) {
        const entry = Object.freeze({sequence: this.nextSequence++, protocol, direction, line});
        this.entries.push(entry);
        this.emit('entry', entry);
        return entry;
    }

    waitFor(predicate, timeoutMs = 2000) {
        const existing = this.entries.find(predicate);
        if (existing) return Promise.resolve(existing);
        return new Promise((resolve, reject) => {
            const timeout = setTimeout(() => {
                this.off('entry', onEntry);
                reject(new Error(`timed out awaiting protocol transcript after ${timeoutMs}ms`));
            }, timeoutMs);
            const onEntry = (entry) => {
                if (!predicate(entry)) return;
                clearTimeout(timeout);
                this.off('entry', onEntry);
                resolve(entry);
            };
            this.on('entry', onEntry);
        });
    }
}

function lineReader(socket, onLine) {
    let buffer = '';
    socket.setEncoding('utf8');
    socket.on('data', (chunk) => {
        buffer += chunk;
        for (;;) {
            const index = buffer.indexOf('\r\n');
            if (index < 0) break;
            const line = buffer.slice(0, index);
            buffer = buffer.slice(index + 2);
            onLine(line);
        }
    });
}

function messageSource(message) {
    const attachment = message.attachments[0];
    if (!attachment) {
        return [
            `Message-ID: <${message.id}@example.test>`,
            `From: ${message.from}`,
            `To: ${message.to}`,
            `Subject: ${message.subject}`,
            'Content-Type: text/plain; charset=utf-8',
            '',
            message.body,
        ].join('\r\n');
    }
    return [
        `Message-ID: <${message.id}@example.test>`,
        `From: ${message.from}`,
        `To: ${message.to}`,
        `Subject: ${message.subject}`,
        'MIME-Version: 1.0',
        'Content-Type: multipart/mixed; boundary="fixed-boundary"',
        '',
        '--fixed-boundary',
        'Content-Type: text/plain; charset=utf-8',
        '',
        message.body,
        '--fixed-boundary',
        `Content-Type: ${attachment.mediaType}; name="${attachment.filename}"`,
        `Content-Disposition: attachment; filename="${attachment.filename}"`,
        'Content-Transfer-Encoding: base64',
        '',
        Buffer.from(attachment.contents).toString('base64'),
        '--fixed-boundary--',
    ].join('\r\n');
}

class FakeMailServer {
    constructor() {
        this.transcript = new ProtocolTranscript();
        this.barriers = new Map();
        this.connections = new Set();
        this.smtpMessages = [];
        this.imapServer = null;
        this.smtpServer = null;
    }

    barrier(name) {
        if (!this.barriers.has(name)) this.barriers.set(name, new ReplyBarrier(name));
        return this.barriers.get(name);
    }

    async start() {
        this.imapServer = net.createServer((socket) => this.handleImap(socket));
        this.smtpServer = net.createServer((socket) => this.handleSmtp(socket));
        await Promise.all([this.listen(this.imapServer), this.listen(this.smtpServer)]);
        return Object.freeze({
            host: '127.0.0.1',
            imapPort: this.imapServer.address().port,
            smtpPort: this.smtpServer.address().port,
            username: FIXTURE.credentials.username,
            password: FIXTURE.credentials.password,
        });
    }

    async stop() {
        for (const socket of this.connections) socket.destroy();
        this.connections.clear();
        await Promise.all([this.close(this.imapServer), this.close(this.smtpServer)]);
    }

    listen(server) {
        server.listen(0, '127.0.0.1');
        return once(server, 'listening');
    }

    close(server) {
        if (!server?.listening) return Promise.resolve();
        return new Promise((resolve, reject) => server.close((error) => error ? reject(error) : resolve()));
    }

    write(socket, protocol, line) {
        this.transcript.push(protocol, 'server', line);
        socket.write(`${line}\r\n`);
    }

    async gatedWrite(socket, protocol, barrierName, lines) {
        await this.barrier(barrierName).pass();
        if (socket.destroyed) return;
        for (const line of lines) this.write(socket, protocol, line);
    }

    handleImap(socket) {
        this.connections.add(socket);
        socket.on('close', () => this.connections.delete(socket));
        this.write(socket, 'imap', '* OK Maho deterministic IMAP ready');
        lineReader(socket, (line) => {
            this.transcript.push('imap', 'client', line);
            void this.onImapLine(socket, line);
        });
    }

    async onImapLine(socket, line) {
        const match = /^(\S+)\s+(\S+)(?:\s+(.*))?$/.exec(line);
        if (!match) return;
        const [, tag, rawCommand, args = ''] = match;
        const command = rawCommand.toUpperCase();
        if (command === 'CAPABILITY') {
            return this.gatedWrite(socket, 'imap', 'imap.capability', [
                '* CAPABILITY IMAP4rev1 UIDPLUS IDLE', `${tag} OK CAPABILITY completed`,
            ]);
        }
        if (command === 'LOGIN') {
            const accepted = args.includes(FIXTURE.credentials.username) && args.includes(FIXTURE.credentials.password);
            return this.gatedWrite(socket, 'imap', 'imap.login', [
                `${tag} ${accepted ? 'OK LOGIN completed' : 'NO invalid credentials'}`,
            ]);
        }
        if (command === 'LIST') {
            const lines = FIXTURE.folders
                .filter((folder) => folder.accountId === 'account-alice')
                .map((folder) => `* LIST (${folder.kind === 'inbox' ? '\\Inbox' : ''}) "/" "${folder.name}"`);
            lines.push(`${tag} OK LIST completed`);
            return this.gatedWrite(socket, 'imap', 'imap.list', lines);
        }
        if (command === 'SELECT') {
            return this.gatedWrite(socket, 'imap', 'imap.select', [
                '* 2 EXISTS', '* 2 RECENT', '* OK [UIDVALIDITY 1] fixed', `${tag} OK [READ-WRITE] SELECT completed`,
            ]);
        }
        if (command === 'UID' && /^FETCH/i.test(args)) {
            const lines = FIXTURE.messages
                .filter((message) => message.accountId === 'account-alice')
                .map((message, index) => {
                    const source = messageSource(message);
                    return `* ${index + 1} FETCH (UID ${message.uid} FLAGS (${message.unread ? '' : '\\Seen'}) BODY[] {${Buffer.byteLength(source)}}\r\n${source})`;
                });
            lines.push(`${tag} OK UID FETCH completed`);
            return this.gatedWrite(socket, 'imap', 'imap.fetch', lines);
        }
        if (command === 'LOGOUT') {
            await this.gatedWrite(socket, 'imap', 'imap.logout', ['* BYE closing', `${tag} OK LOGOUT completed`]);
            return socket.end();
        }
        return this.gatedWrite(socket, 'imap', 'imap.unknown', [`${tag} BAD unsupported command`]);
    }

    handleSmtp(socket) {
        this.connections.add(socket);
        socket.on('close', () => this.connections.delete(socket));
        const state = {mailFrom: '', recipients: [], data: null};
        this.write(socket, 'smtp', '220 maho.example.test ESMTP ready');
        lineReader(socket, (line) => {
            this.transcript.push('smtp', 'client', line);
            void this.onSmtpLine(socket, state, line);
        });
    }

    async onSmtpLine(socket, state, line) {
        if (state.data) {
            if (line === '.') {
                this.smtpMessages.push(Object.freeze({
                    mailFrom: state.mailFrom,
                    recipients: Object.freeze([...state.recipients]),
                    data: state.data.join('\r\n'),
                }));
                state.data = null;
                return this.gatedWrite(socket, 'smtp', 'smtp.accept', ['250 2.0.0 queued as fixed-1']);
            }
            state.data.push(line.startsWith('..') ? line.slice(1) : line);
            return;
        }
        const command = line.split(/\s+/, 1)[0].toUpperCase();
        if (command === 'EHLO' || command === 'HELO') {
            return this.gatedWrite(socket, 'smtp', 'smtp.greeting', ['250-maho.example.test', '250 SIZE 1048576']);
        }
        if (command === 'AUTH') return this.gatedWrite(socket, 'smtp', 'smtp.auth', ['235 2.7.0 authenticated']);
        if (command === 'MAIL') {
            state.mailFrom = line.slice(5);
            return this.gatedWrite(socket, 'smtp', 'smtp.mail', ['250 2.1.0 sender ok']);
        }
        if (command === 'RCPT') {
            state.recipients.push(line.slice(5));
            return this.gatedWrite(socket, 'smtp', 'smtp.rcpt', ['250 2.1.5 recipient ok']);
        }
        if (command === 'DATA') {
            state.data = [];
            return this.gatedWrite(socket, 'smtp', 'smtp.data', ['354 end with <CRLF>.<CRLF>']);
        }
        if (command === 'RSET') {
            state.mailFrom = '';
            state.recipients = [];
            state.data = null;
            return this.gatedWrite(socket, 'smtp', 'smtp.reset', ['250 2.0.0 reset']);
        }
        if (command === 'QUIT') {
            await this.gatedWrite(socket, 'smtp', 'smtp.quit', ['221 2.0.0 bye']);
            return socket.end();
        }
        return this.gatedWrite(socket, 'smtp', 'smtp.unknown', ['502 5.5.1 unsupported command']);
    }
}

module.exports = {FakeMailServer, ReplyBarrier, ProtocolTranscript};
