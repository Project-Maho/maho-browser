'use strict';

const net = require('node:net');
const tls = require('node:tls');
const {readFileSync} = require('node:fs');
const path = require('node:path');
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

const quote = value => JSON.stringify(String(value));
function address(value) {
    const m = /^(?:(.*?)\s*<)?([^<>@ ]+)@([^<> ]+)>?$/.exec(value);
    return `((${m[1] ? quote(m[1]) : 'NIL'} NIL ${quote(m[2])} ${quote(m[3])}))`;
}
function envelope(m) {
    const from = address(m.from);
    return `("Sat, 05 Sep 2026 00:00:00 +0000" ${quote(m.subject)} ${from} ${from} ${from} ${address(m.to)} NIL NIL NIL ${quote(`<${m.id}@example.test>`)})`;
}
function structure(m) {
    const text = `("TEXT" "PLAIN" ("CHARSET" "utf-8") NIL NIL "8BIT" ${Buffer.byteLength(m.body)} ${m.body.split('\n').length})`;
    const a = m.attachments[0];
    if (!a) return text;
    const [type, subtype] = a.mediaType.toUpperCase().split('/');
    return `(${text} (${quote(type)} ${quote(subtype)} ("NAME" ${quote(a.filename)}) NIL NIL "BASE64" ${Buffer.from(a.contents).toString('base64').length}${type === 'TEXT' ? ' 1' : ''} NIL ("ATTACHMENT" ("FILENAME" ${quote(a.filename)}))) "MIXED")`;
}
function inSet(value, set, maximum) {
    return set.split(',').some(range => {
        const ends = range.split(':').map(n => n === '*' ? maximum : Number(n));
        return value >= Math.min(...ends) && value <= Math.max(...ends);
    });
}
function unquote(value) { return value.startsWith('"') ? JSON.parse(value) : value; }

class FakeMailServer {
    constructor(options = {}) {
        this.options = options;
        this.transcript = new ProtocolTranscript();
        this.barriers = new Map();
        this.connections = new Set();
        this.smtpMessages = [];
        this.smtpAttempts = [];
        this.smtpRejection = null;
        this.mailboxes = new Map(FIXTURE.folders.filter(f => f.accountId === 'account-alice')
            .map(f => [f.name, {epoch: 1, nextUid: 1, messages: []}]));
        for (const message of FIXTURE.messages.filter(m => m.accountId === 'account-alice')) {
            this.arrive('INBOX', message);
        }
        for (let i = 2; i < (options.historyCount || 2); i++) this.arrive('INBOX', {subject: `History ${i + 1}`});
        this.imapServer = null;
        this.smtpServer = null;
    }

    barrier(name) {
        if (!this.barriers.has(name)) this.barriers.set(name, new ReplyBarrier(name));
        return this.barriers.get(name);
    }

    arrive(folder, fields = {}) {
        const box = this.mailboxes.get(folder);
        if (!box) throw new Error(`unknown folder ${folder}`);
        const uid = fields.uid ?? box.nextUid;
        if (box.messages.some(m => m.uid === uid)) throw new Error('duplicate UID');
        const message = {...FIXTURE.messages[0], id: `arrival-${uid}`, ...fields, uid,
            flags: fields.flags ? [...fields.flags] : [fields.unread === false ? '\\Seen' : '', fields.starred ? '\\Flagged' : ''].filter(Boolean)};
        box.messages.push(message);
        box.nextUid = Math.max(box.nextUid, uid + 1);
        this.transcript.push('control', 'state', JSON.stringify({command: 'arrive', folder, uid}));
        for (const socket of this.connections) {
            if (socket.imapState?.folder === folder && socket.imapState.idle) this.write(socket, 'imap', `* ${box.messages.length} EXISTS`);
        }
        return message;
    }

    setFlags(folder, uid, flags) {
        const message = this.mailboxes.get(folder)?.messages.find(m => m.uid === uid);
        if (!message) throw new Error('unknown message');
        message.flags = [...flags];
        this.transcript.push('control', 'state', JSON.stringify({command: 'flags', folder, uid, flags}));
    }

    resetEpoch(folder, epoch) {
        const box = this.mailboxes.get(folder);
        if (!box || !Number.isInteger(epoch) || epoch <= box.epoch) throw new Error('epoch must increase');
        box.epoch = epoch;
        box.messages.forEach((m, i) => { m.uid = i + 1; });
        box.nextUid = box.messages.length + 1;
        this.transcript.push('control', 'state', JSON.stringify({command: 'epoch', folder, epoch}));
    }

    rejectNextSmtp(reply = '550 5.7.1 fixture rejection') {
        if (!/^[45]\d\d [^\r\n]+$/.test(reply)) throw new Error('invalid SMTP rejection');
        this.smtpRejection = reply;
    }

    async start() {
        const create = this.options.tls ? (handler) => tls.createServer({
            key: readFileSync(path.join(__dirname, 'tls-assets/localhost-key.pem')),
            cert: readFileSync(path.join(__dirname, 'tls-assets/localhost-cert.pem')),
        }, handler) : (handler) => net.createServer(handler);
        this.imapServer = create((socket) => this.handleImap(socket));
        this.smtpServer = create((socket) => this.handleSmtp(socket));
        for (const server of [this.imapServer, this.smtpServer]) {
            server.on('connection', socket => {
                this.connections.add(socket);
                socket.on('close', () => this.connections.delete(socket));
                socket.on('error', error => this.transcript.push('transport', 'error', error.message));
            });
            server.on('tlsClientError', error => this.transcript.push('tls', 'error', error.message));
        }
        try {
            await this.listen(this.imapServer, this.options.imapPort);
            await this.listen(this.smtpServer, this.options.smtpPort);
        } catch (error) { await this.stop(); throw error; }
        return Object.freeze({
            host: '127.0.0.1',
            tls: Boolean(this.options.tls),
            imapPort: this.imapServer.address().port,
            smtpPort: this.smtpServer.address().port,
            username: FIXTURE.credentials.username,
            password: FIXTURE.credentials.password,
        });
    }

    async stop() {
        for (const socket of this.connections) socket.destroy();
        for (const barrier of this.barriers.values()) barrier.release();
        this.connections.clear();
        await Promise.all([this.close(this.imapServer), this.close(this.smtpServer)]);
    }

    listen(server, port = 0) {
        const listening = once(server, 'listening');
        server.listen(port, '127.0.0.1');
        return listening;
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
        socket.imapState = {folder: 'INBOX', authenticated: !this.options.tls, idle: null};
        let pending = Promise.resolve();
        this.write(socket, 'imap', '* OK Maho deterministic IMAP ready');
        lineReader(socket, (line) => {
            this.transcript.push('imap', 'client', line);
            pending = pending.then(() => this.onImapLine(socket, line)).catch(error => {
                this.transcript.push('imap', 'error', error.message);
                socket.destroy();
            });
        });
    }

    async onImapLine(socket, line) {
        const state = socket.imapState;
        if (line === 'DONE' && state.idle) {
            this.write(socket, 'imap', `${state.idle} OK IDLE completed`);
            state.idle = null;
            return;
        }
        const match = /^(\S+)\s+(\S+)(?:\s+(.*))?$/.exec(line);
        if (!match) return;
        const [, tag, rawCommand, args = ''] = match;
        const command = rawCommand.toUpperCase();
        if (command === 'CAPABILITY') {
            return this.gatedWrite(socket, 'imap', 'imap.capability', [
                '* CAPABILITY IMAP4rev1 UIDPLUS MOVE IDLE', `${tag} OK CAPABILITY completed`,
            ]);
        }
        if (command === 'LOGIN') {
            const tokens = args.match(/"(?:\\.|[^"\\])*"|\S+/g) || [];
            const accepted = tokens.length === 2 && unquote(tokens[0]) === FIXTURE.credentials.username && unquote(tokens[1]) === FIXTURE.credentials.password;
            state.authenticated = accepted;
            return this.gatedWrite(socket, 'imap', 'imap.login', [
                `${tag} ${accepted ? 'OK LOGIN completed' : 'NO invalid credentials'}`,
            ]);
        }
        if (!state.authenticated) return this.write(socket, 'imap', `${tag} NO authenticate first`);
        if (command === 'LIST') {
            const lines = FIXTURE.folders
                .filter((folder) => folder.accountId === 'account-alice')
                .map((folder) => `* LIST (${folder.kind === 'inbox' ? '\\Inbox' : ''}) "/" "${folder.name}"`);
            lines.push(`${tag} OK LIST completed`);
            return this.gatedWrite(socket, 'imap', 'imap.list', lines);
        }
        if (command === 'SELECT') {
            const folder = unquote(args);
            const box = this.mailboxes.get(folder);
            if (!box) return this.write(socket, 'imap', `${tag} NO no such folder`);
            state.folder = folder;
            return this.gatedWrite(socket, 'imap', 'imap.select', [
                `* ${box.messages.length} EXISTS`, '* FLAGS (\\Seen \\Flagged \\Deleted)',
                `* OK [UIDVALIDITY ${box.epoch}] fixed`, `* OK [UIDNEXT ${box.nextUid}] next`, `${tag} OK [READ-WRITE] SELECT completed`,
            ]);
        }
        const uidMode = command === 'UID';
        const operation = uidMode ? args.split(' ')[0].toUpperCase() : command;
        const parameters = uidMode ? args.slice(args.indexOf(' ') + 1) : args;
        const box = this.mailboxes.get(state.folder);
        const [set, ...rest] = parameters.split(' ');
        const query = rest.join(' ');
        const matches = box.messages.map((m, i) => ({m, seq: i + 1})).filter(({m, seq}) => inSet(uidMode ? m.uid : seq, set, uidMode ? Math.max(0, ...box.messages.map(m => m.uid)) : box.messages.length));
        if (operation === 'FETCH') {
            const lines = matches.map(({m, seq}) => {
                const values = [`UID ${m.uid}`];
                const body = /BODY(\.PEEK)?\[([^\]]*)\]/i.exec(query);
                if ((body && !body[1]) || /\bRFC822\b(?!\.SIZE)/i.test(query)) {
                    if (!m.flags.includes('\\Seen')) m.flags.push('\\Seen');
                }
                if (/\bFLAGS\b/i.test(query)) values.push(`FLAGS (${m.flags.join(' ')})`);
                if (/\bENVELOPE\b/i.test(query)) values.push(`ENVELOPE ${envelope(m)}`);
                if (/RFC822.SIZE/i.test(query)) values.push(`RFC822.SIZE ${Buffer.byteLength(messageSource(m))}`);
                if (/BODYSTRUCTURE/i.test(query)) values.push(`BODYSTRUCTURE ${structure(m)}`);
                if (body || /\bRFC822\b(?!\.SIZE)/i.test(query)) {
                    const section = body?.[2] || '';
                    const a = m.attachments[0];
                    const source = section === '' ? messageSource(m) : section === '1' ? m.body : section === '2' && a ? Buffer.from(a.contents).toString('base64') : null;
                    const key = body ? `BODY[${section}]` : 'RFC822';
                    values.push(source === null ? `${key} NIL` : `${key} {${Buffer.byteLength(source)}}\r\n${source}`);
                }
                return `* ${seq} FETCH (${values.join(' ')})`;
            });
            lines.push(`${tag} OK ${uidMode ? 'UID ' : ''}FETCH completed`);
            return this.gatedWrite(socket, 'imap', 'imap.fetch', lines);
        }
        if (operation === 'STORE') {
            const flags = /\(([^)]*)\)/.exec(query)?.[1].split(/\s+/).filter(Boolean) || [];
            for (const {m, seq} of matches) {
                m.flags = query.startsWith('+') ? [...new Set([...m.flags, ...flags])] : query.startsWith('-') ? m.flags.filter(f => !flags.includes(f)) : flags;
                if (!/SILENT/i.test(query)) this.write(socket, 'imap', `* ${seq} FETCH (UID ${m.uid} FLAGS (${m.flags.join(' ')}))`);
            }
            return this.write(socket, 'imap', `${tag} OK STORE completed`);
        }
        if (operation === 'MOVE' || operation === 'COPY') {
            const target = unquote(query);
            if (!this.mailboxes.has(target)) return this.write(socket, 'imap', `${tag} NO no such folder`);
            const copied = matches.map(({m}) => this.arrive(target, {...m, uid: undefined}).uid);
            if (operation === 'MOVE') {
                for (const {m} of matches) {
                    const index = box.messages.indexOf(m);
                    box.messages.splice(index, 1);
                    this.write(socket, 'imap', `* ${index + 1} EXPUNGE`);
                }
            }
            return this.write(socket, 'imap', `${tag} OK ${matches.length ? `[COPYUID ${this.mailboxes.get(target).epoch} ${matches.map(({m}) => m.uid).join(',')} ${copied.join(',')}] ` : ''}${operation} completed`);
        }
        if (operation === 'EXPUNGE') {
            for (const m of [...box.messages]) {
                if (m.flags.includes('\\Deleted') && (!uidMode || matches.some(item => item.m === m))) {
                    const index = box.messages.indexOf(m);
                    box.messages.splice(index, 1);
                    this.write(socket, 'imap', `* ${index + 1} EXPUNGE`);
                }
            }
            return this.write(socket, 'imap', `${tag} OK EXPUNGE completed`);
        }
        if (command === 'IDLE') { state.idle = tag; return this.write(socket, 'imap', '+ idling'); }
        if (command === 'NOOP') return this.write(socket, 'imap', `${tag} OK NOOP completed`);
        if (command === 'LOGOUT') {
            await this.gatedWrite(socket, 'imap', 'imap.logout', ['* BYE closing', `${tag} OK LOGOUT completed`]);
            return socket.end();
        }
        return this.gatedWrite(socket, 'imap', 'imap.unknown', [`${tag} BAD unsupported command`]);
    }

    handleSmtp(socket) {
        this.connections.add(socket);
        socket.on('close', () => this.connections.delete(socket));
        const state = {mailFrom: '', recipients: [], data: null, authenticated: !this.options.tls, auth: null};
        let pending = Promise.resolve();
        this.write(socket, 'smtp', '220 maho.example.test ESMTP ready');
        lineReader(socket, (line) => {
            this.transcript.push('smtp', 'client', line);
            pending = pending.then(() => this.onSmtpLine(socket, state, line)).catch(error => {
                this.transcript.push('smtp', 'error', error.message);
                socket.destroy();
            });
        });
    }

    async onSmtpLine(socket, state, line) {
        if (state.data) {
            if (line === '.') {
                const message = Object.freeze({
                    mailFrom: state.mailFrom,
                    recipients: Object.freeze([...state.recipients]),
                    data: state.data.join('\r\n'),
                });
                const reply = this.smtpRejection || '250 2.0.0 queued as fixed-1';
                const accepted = !this.smtpRejection;
                this.smtpRejection = null;
                this.smtpAttempts.push(Object.freeze({accepted, reply, message}));
                if (accepted) this.smtpMessages.push(message);
                state.data = null;
                state.mailFrom = '';
                state.recipients = [];
                return this.gatedWrite(socket, 'smtp', 'smtp.accept', [reply]);
            }
            state.data.push(line.startsWith('..') ? line.slice(1) : line);
            return;
        }
        const authenticate = (username, password) => {
            state.auth = null;
            state.authenticated = username === FIXTURE.credentials.username && password === FIXTURE.credentials.password;
            return this.gatedWrite(socket, 'smtp', 'smtp.auth', [state.authenticated ? '235 2.7.0 authenticated' : '535 5.7.8 invalid credentials']);
        };
        if (state.auth === 'plain') {
            const parts = Buffer.from(line, 'base64').toString().split('\0');
            return authenticate(parts[1], parts[2]);
        }
        if (state.auth === 'username') {
            state.username = Buffer.from(line, 'base64').toString();
            state.auth = 'password';
            return this.write(socket, 'smtp', '334 UGFzc3dvcmQ6');
        }
        if (state.auth === 'password') return authenticate(state.username, Buffer.from(line, 'base64').toString());
        const command = line.split(/\s+/, 1)[0].toUpperCase();
        if (command === 'EHLO' || command === 'HELO') {
            return this.gatedWrite(socket, 'smtp', 'smtp.greeting', ['250-maho.example.test', '250-AUTH PLAIN LOGIN', '250 SIZE 1048576']);
        }
        if (command === 'AUTH') {
            const [, mechanism, token] = line.split(' ');
            if (mechanism.toUpperCase() === 'PLAIN') {
                if (token) {
                    const parts = Buffer.from(token, 'base64').toString().split('\0');
                    return authenticate(parts[1], parts[2]);
                }
                state.auth = 'plain';
                return this.write(socket, 'smtp', '334 ');
            }
            if (mechanism.toUpperCase() === 'LOGIN') {
                state.auth = token ? 'password' : 'username';
                state.username = token ? Buffer.from(token, 'base64').toString() : '';
                return this.write(socket, 'smtp', token ? '334 UGFzc3dvcmQ6' : '334 VXNlcm5hbWU6');
            }
            return this.write(socket, 'smtp', '504 unsupported authentication');
        }
        if (command === 'NOOP') return this.write(socket, 'smtp', '250 2.0.0 ok');
        if (['MAIL', 'RCPT', 'DATA'].includes(command) && !state.authenticated) return this.write(socket, 'smtp', '530 authentication required');
        if (command === 'MAIL') {
            state.mailFrom = line.slice(5);
            state.recipients = [];
            return this.gatedWrite(socket, 'smtp', 'smtp.mail', ['250 2.1.0 sender ok']);
        }
        if (command === 'RCPT') {
            if (!state.mailFrom) return this.write(socket, 'smtp', '503 sender required');
            state.recipients.push(line.slice(5));
            return this.gatedWrite(socket, 'smtp', 'smtp.rcpt', ['250 2.1.5 recipient ok']);
        }
        if (command === 'DATA') {
            if (!state.recipients.length) return this.write(socket, 'smtp', '503 recipient required');
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

async function main() {
    const args = process.argv.slice(2);
    const number = (name, fallback) => {
        const arg = args.find(a => a.startsWith(`--${name}=`));
        const value = arg ? Number(arg.split('=')[1]) : fallback;
        if (!Number.isInteger(value) || value < 0) throw new Error(`invalid ${name}`);
        return value;
    };
    const server = new FakeMailServer({tls: args.includes('--tls'), imapPort: number('imap-port', 1993), smtpPort: number('smtp-port', 1465), historyCount: number('history', 2)});
    const emit = value => process.stdout.write(`${JSON.stringify(value)}\n`);
    server.transcript.on('entry', entry => emit({type: 'event', ...entry}));
    const address = await server.start();
    emit({type: 'ready', address});
    const input = require('node:readline').createInterface({input: process.stdin});
    let stopping = false;
    const stop = async () => {
        if (stopping) return;
        stopping = true;
        await server.stop();
        input.close();
        process.stdin.pause();
        emit({type: 'stopped'});
    };
    process.once('SIGTERM', stop);
    process.once('SIGINT', stop);
    input.on('close', stop);
    for await (const line of input) {
        let request;
        try {
            request = JSON.parse(line);
            let result = null;
            switch (request.command) {
                case 'arrive': result = server.arrive(request.folder, request.message); break;
                case 'flags': server.setFlags(request.folder, request.uid, request.flags); break;
                case 'epoch': server.resetEpoch(request.folder, request.epoch); break;
                case 'reject-smtp': server.rejectNextSmtp(request.reply); break;
                case 'block': server.barrier(request.name).block(); break;
                case 'release': server.barrier(request.name).release(); break;
                case 'snapshot': result = {folders: Object.fromEntries(server.mailboxes), smtpAttempts: server.smtpAttempts}; break;
                case 'stop': await stop(); return;
                default: throw new Error('unknown command');
            }
            emit({type: 'result', id: request.id, result});
        } catch (error) { emit({type: 'error', id: request?.id, error: error.message}); }
    }
}
if (require.main === module) main().catch(error => { console.error(error); process.exitCode = 1; });
