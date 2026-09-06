'use strict';

const assert = require('node:assert/strict');
const {test} = require('node:test');
const tls = require('node:tls');
const net = require('node:net');
const {readFileSync} = require('node:fs');
const {once, EventEmitter} = require('node:events');
const {spawn} = require('node:child_process');
const path = require('node:path');
const {FakeMailServer} = require('./fake-mail-server');
const {FIXTURE} = require('./fixed-mail-fixture');

const ca = readFileSync(path.join(__dirname, 'tls-assets/localhost-cert.pem'));
const bounded = (emitter, event) => once(emitter, event, {signal: AbortSignal.timeout(3000)});

// Accumulate actual client-received bytes, not server-side "write attempted" events.
class Wire extends EventEmitter {
    constructor(socket) {
        super();
        this.socket = socket;
        this.buffer = '';
        socket.setEncoding('utf8');
        socket.on('data', (data) => { this.buffer += data; this.emit('bytes'); });
        socket.on('error', (error) => this.emit('error', error));
        this.tag = 0;
    }

    async until(pattern) {
        const deadline = AbortSignal.timeout(3000);
        while (!pattern.test(this.buffer)) await once(this, 'bytes', {signal: deadline});
        const end = this.buffer.match(pattern);
        const length = end.index + end[0].length;
        const response = this.buffer.slice(0, length);
        this.buffer = this.buffer.slice(length);
        return response;
    }

    async command(line, pattern) {
        const reply = this.until(pattern);
        this.socket.write(`${line}\r\n`);
        return reply;
    }

    async imap(command, status = 'OK') {
        const tag = `T${++this.tag}`;
        const response = await this.command(`${tag} ${command}`, new RegExp(`${tag} (?:OK|NO|BAD)[^\\r]*\\r\\n`));
        assert.match(response, new RegExp(`${tag} ${status} `));
        return response;
    }
}

async function setup(t, options = {}) {
    const server = new FakeMailServer({tls: true, ...options});
    t.after(() => server.stop());
    const address = await server.start();
    assert.equal(address.tls, true);
    async function connect(protocol) {
        const socket = tls.connect({host: address.host, port: address[`${protocol}Port`], servername: 'localhost', ca});
        const wire = new Wire(socket);
        t.after(() => socket.destroy());
        await bounded(socket, 'secureConnect');
        assert.equal(socket.authorized, true);
        await wire.until(protocol === 'imap' ? /\* OK[^\r]*\r\n/ : /220[^\r]*\r\n/);
        return wire;
    }
    const imap = await connect('imap');
    await imap.imap('LOGIN "alice@example.test" "wrong"', 'NO');
    await imap.imap('LOGIN "alice@example.test" "fixture-password"');
    return {server, address, imap, connect};
}

function uids(response) {
    return [...response.matchAll(/\* \d+ FETCH \(UID (\d+)/g)].map((m) => Number(m[1]));
}

function literal(response, section) {
    const marker = `BODY[${section}] {`;
    const start = response.indexOf(marker);
    assert.notEqual(start, -1);
    const headerEnd = response.indexOf('\r\n', start);
    const size = Number(response.slice(start + marker.length, headerEnd - 1));
    const bytes = Buffer.from(response.slice(headerEnd + 2));
    assert.equal(bytes.subarray(size, size + 1).toString(), ')');
    return bytes.subarray(0, size).toString();
}

test('TLS SELECT and bounded FETCH expose real metadata, attachment and read-only PEEK', async (t) => {
    const {imap} = await setup(t);
    assert.match(await imap.imap('CAPABILITY'), /UIDPLUS MOVE IDLE/);
    assert.match(await imap.imap('LIST "" "*"'), /"Sent"/);
    const selected = await imap.imap('SELECT "INBOX"');
    assert.match(selected, /\* 2 EXISTS/);
    assert.match(selected, /\[UIDVALIDITY 1\]/);
    const response = await imap.imap('FETCH 2:2 (UID FLAGS ENVELOPE RFC822.SIZE BODYSTRUCTURE)');
    assert.deepEqual(uids(response), [102]);
    assert.match(response, /FLAGS \(\\Flagged\)/);
    assert.match(response, /ENVELOPE \("Sat, 05 Sep 2026 00:00:00 \+0000" "Quarterly attachment" \(\("Carol" NIL "carol" "example.test"\)\)/);
    assert.match(response, /"ATTACHMENT" \("FILENAME" "report.txt"\)/);
    const attachmentSize = Number(response.match(/"BASE64" (\d+) 1/)[1]);
    assert.doesNotMatch(response, /BODY\[\]/);
    assert.deepEqual(uids(await imap.imap('UID FETCH 101:101 (UID FLAGS)')), [101]);
    const source = literal(await imap.imap('UID FETCH 102 BODY.PEEK[]'), '');
    assert.match(source, /Content-Disposition: attachment; filename="report.txt"/);
    const size = Number(response.match(/RFC822.SIZE (\d+)/)[1]);
    assert.equal(size, Buffer.byteLength(source));
    const part = literal(await imap.imap('UID FETCH 102 (BODYSTRUCTURE BODY.PEEK[2])'), '2');
    assert.equal(attachmentSize, Buffer.byteLength(part));
    assert.equal(Buffer.from(part, 'base64').toString(), FIXTURE.messages[1].attachments[0].contents);
    assert.doesNotMatch(await imap.imap('UID FETCH 102 FLAGS'), /\\Seen/);
    await imap.imap('UID FETCH 102 BODY[]');
    assert.match(await imap.imap('UID FETCH 102 FLAGS'), /\\Seen/);
    assert.match(await imap.imap('SELECT Sent'), /\* 0 EXISTS/);
    assert.deepEqual(uids(await imap.imap('UID FETCH 1:* FLAGS')), []);
    await imap.imap('SELECT absent', 'NO');
    await imap.imap('LOGOUT');
});

test('UID MOVE and targeted EXPUNGE change membership without unrelated deletion', async (t) => {
    const {imap, server} = await setup(t);
    server.arrive('INBOX', {subject: 'unrelated'});
    await imap.imap('SELECT INBOX');
    await imap.imap('UID STORE 103 +FLAGS (\\Deleted)');
    await imap.imap('UID MOVE 101 "Sent"');
    assert.deepEqual(uids(await imap.imap('UID FETCH 1:* FLAGS')), [102, 103]);
    await imap.imap('UID STORE 102 +FLAGS (\\Deleted)');
    assert.match(await imap.imap('UID EXPUNGE 102'), /\* 1 EXPUNGE/);
    assert.deepEqual(uids(await imap.imap('UID FETCH 1:* FLAGS')), [103]);
    assert.match(await imap.imap('UID FETCH 103 FLAGS'), /\\Deleted/);
    await imap.imap('SELECT Sent');
    assert.deepEqual(uids(await imap.imap('UID FETCH 1:* FLAGS')), [1]);
    assert.match(literal(await imap.imap('UID FETCH 1 BODY.PEEK[]'), ''), /Fixed body alpha/);
    await imap.imap('UID COPY 1 Drafts');
    assert.deepEqual(uids(await imap.imap('UID FETCH 1:* FLAGS')), [1]);
    await imap.imap('SELECT Drafts');
    assert.deepEqual(uids(await imap.imap('UID FETCH 1:* FLAGS')), [1]);
});

test('history, remote flags, IDLE arrival, epoch reset and barrier cleanup are event-driven', async (t) => {
    const {imap, server} = await setup(t, {historyCount: 510});
    assert.match(await imap.imap('SELECT INBOX'), /\* 510 EXISTS/);
    assert.equal(uids(await imap.imap('FETCH 11:510 (UID FLAGS ENVELOPE RFC822.SIZE BODYSTRUCTURE)')).length, 500);
    assert.deepEqual(uids(await imap.imap('UID FETCH 101:102 FLAGS')), [101, 102]);
    server.setFlags('INBOX', 101, ['\\Seen']);
    assert.match(await imap.imap('UID FETCH 101 FLAGS'), /\\Seen/);
    await imap.command('ID1 IDLE', /\+ idling\r\n/);
    const exists = imap.until(/\* 511 EXISTS\r\n/);
    const arrived = server.arrive('INBOX', {subject: 'arrival', body: 'caf\u00e9'});
    await exists;
    await imap.command('DONE', /ID1 OK[^\r]*\r\n/);
    assert.deepEqual(uids(await imap.imap(`UID FETCH ${arrived.uid} FLAGS`)), [611]);
    assert.match(literal(await imap.imap('UID FETCH 611 BODY.PEEK[]'), ''), /caf\u00e9/);
    server.resetEpoch('INBOX', 7);
    assert.match(await imap.imap('SELECT INBOX'), /\[UIDVALIDITY 7\]/);
    assert.deepEqual(uids(await imap.imap('UID FETCH 1:2 FLAGS')), [1, 2]);
    server.barrier('imap.fetch').block();
    const after = server.transcript.nextSequence - 1;
    const seen = server.transcript.waitFor((e) => e.sequence > after && e.direction === 'client' && e.line === 'B1 UID FETCH 1 FLAGS');
    imap.socket.write('B1 UID FETCH 1 FLAGS\r\n');
    await seen;
    assert.equal(server.transcript.entries.some((e) => e.line.startsWith('B1 OK')), false);
    const done = imap.until(/B1 OK[^\r]*\r\n/);
    server.barrier('imap.fetch').release();
    await done;
    server.barrier('imap.fetch').block();
    const blocked = server.transcript.waitFor((e) => e.line === 'B2 UID FETCH 1 FLAGS');
    imap.socket.write('B2 UID FETCH 1 FLAGS\r\n');
    await blocked;
    const closed = bounded(imap.socket, 'close');
    await server.stop();
    await closed;
    assert.equal(server.connections.size, 0);
    assert.equal(server.imapServer.listening, false);
    assert.equal(server.smtpServer.listening, false);
    assert.equal(server.barrier('imap.fetch').waiters.length, 0);
    assert.equal(server.transcript.listenerCount('entry'), 0);
});

test('TLS SMTP AUTH, accepted and rejected DATA capture exact envelope and dot unstuffing', async (t) => {
    const {server, connect} = await setup(t);
    const smtp = await connect('smtp');
    assert.match(await smtp.command('EHLO localhost', /250 SIZE[^\r]*\r\n/), /250-AUTH PLAIN LOGIN/);
    await smtp.command('AUTH PLAIN ' + Buffer.from('\0alice@example.test\0wrong').toString('base64'), /535[^\r]*\r\n/);
    await smtp.command('AUTH PLAIN ' + Buffer.from('\0alice@example.test\0fixture-password').toString('base64'), /235[^\r]*\r\n/);
    await smtp.command('NOOP', /250[^\r]*\r\n/);
    async function send(expected) {
        await smtp.command('MAIL FROM:<alice@example.test>', /250[^\r]*\r\n/);
        await smtp.command('RCPT TO:<bob@example.test>', /250[^\r]*\r\n/);
        await smtp.command('DATA', /354[^\r]*\r\n/);
        return smtp.command('Subject: wire\r\n\r\n..dot\r\n.', new RegExp(`${expected}[^\\r]*\\r\\n`));
    }
    await send(250);
    server.rejectNextSmtp('550 5.7.1 fixture rejection');
    await send(550);
    assert.equal(server.smtpMessages.length, 1);
    assert.equal(server.smtpAttempts.length, 2);
    assert.deepEqual(server.smtpAttempts.map((a) => a.accepted), [true, false]);
    assert.deepEqual(server.smtpMessages[0], {mailFrom: 'FROM:<alice@example.test>', recipients: ['TO:<bob@example.test>'], data: 'Subject: wire\r\n\r\n.dot'});
    assert.deepEqual(server.smtpAttempts[1].message, server.smtpMessages[0]);
    await smtp.command('QUIT', /221[^\r]*\r\n/);
});

test('standalone TLS JSONL interface exposes readiness, controls, events and clean stop', async (t) => {
    const child = spawn(process.execPath, [path.join(__dirname, 'fake-mail-server.js'), '--tls', '--imap-port=0', '--smtp-port=0', '--history=510']);
    t.after(() => { if (child.exitCode === null) child.kill(); });
    let stderr = '';
    child.stderr.on('data', (chunk) => { stderr += chunk; });
    const output = new Wire(child.stdout);
    const exited = bounded(child, 'exit');
    const ready = JSON.parse(await output.until(/\n/));
    assert.equal(ready.type, 'ready');
    assert.equal(ready.address.tls, true);
    const reply = output.until(/"id":"seed"[^\n]*\n/);
    child.stdin.write('{"id":"seed","command":"arrive","folder":"INBOX","message":{"subject":"CLI arrival"}}\n');
    const lines = (await reply).trim().split('\n').map(JSON.parse);
    assert.equal(lines.at(-1).result.uid, 611);
    const stopped = output.until(/"type":"stopped"[^\n]*\n/);
    child.stdin.write('{"id":"stop","command":"stop"}\n');
    await stopped;
    assert.deepEqual(await exited, [0, null], stderr);
    const probe = net.createConnection({host: '127.0.0.1', port: ready.address.imapPort});
    const [error] = await bounded(probe, 'error');
    assert.equal(error.code, 'ECONNREFUSED');
});
