'use strict';

const assert = require('node:assert/strict');
const {spawnSync} = require('node:child_process');
const {once} = require('node:events');
const net = require('node:net');
const path = require('node:path');
const {afterEach, test} = require('node:test');

const {FIXTURE, assertPublicOnlyFixture} = require('./fixed-mail-fixture.js');
const {FakeMailServer} = require('./fake-mail-server.js');

const servers = new Set();
afterEach(async () => {
    await Promise.all([...servers].map((server) => server.stop()));
    servers.clear();
});

async function connect(port) {
    const socket = net.createConnection({host: '127.0.0.1', port});
    await once(socket, 'connect');
    return socket;
}

async function writeAfterSubscribed(
    server, protocol, command, replyMatcher, socket) {
    const reply = server.transcript.waitFor((entry) =>
        entry.protocol === protocol &&
        entry.direction === 'server' &&
        replyMatcher.test(entry.line));
    socket.write(`${command}\r\n`);
    return reply;
}

test('local IMAP and SMTP transport fixture is barrier-controlled', async () => {
    assertPublicOnlyFixture();
    const server = new FakeMailServer();
    servers.add(server);
    const address = await server.start();
    assert.equal(address.host, '127.0.0.1');

    const imap = await connect(address.imapPort);
    await writeAfterSubscribed(
        server,
        'imap',
        `A1 LOGIN ${address.username} ${address.password}`,
        /A1 OK/,
        imap);
    server.barrier('imap.fetch').block();
    const fetchSeen = server.transcript.waitFor((entry) =>
        entry.protocol === 'imap' &&
        entry.direction === 'client' &&
        entry.line === 'A2 UID FETCH 1:* BODY.PEEK[]');
    imap.write('A2 UID FETCH 1:* BODY.PEEK[]\r\n');
    await fetchSeen;
    assert.equal(
        server.transcript.entries.some((entry) =>
            /A2 OK UID FETCH/.test(entry.line)),
        false);
    const fetchDone = server.transcript.waitFor((entry) =>
        /A2 OK UID FETCH/.test(entry.line));
    server.barrier('imap.fetch').release();
    await fetchDone;

    const smtp = await connect(address.smtpPort);
    await writeAfterSubscribed(
        server, 'smtp', 'EHLO client.example.test', /^250 SIZE/, smtp);
    await writeAfterSubscribed(
        server, 'smtp', 'MAIL FROM:<alice@example.test>', /^250 2\.1\.0/, smtp);
    await writeAfterSubscribed(
        server, 'smtp', 'RCPT TO:<bob@example.test>', /^250 2\.1\.5/, smtp);
    await writeAfterSubscribed(server, 'smtp', 'DATA', /^354 /, smtp);
    const accepted = server.transcript.waitFor((entry) =>
        /^250 2\.0\.0 queued/.test(entry.line));
    smtp.write('Subject: Fixed send\r\n\r\nbody\r\n.\r\n');
    await accepted;
    assert.deepEqual(server.smtpMessages, [{
        mailFrom: 'FROM:<alice@example.test>',
        recipients: ['TO:<bob@example.test>'],
        data: 'Subject: Fixed send\r\n\r\nbody',
    }]);

    imap.destroy();
    smtp.destroy();
});

test('native Mail product contracts are load-bearing', () => {
    const verifier = path.join(__dirname, 'verify-native-mail-contracts.py');
    const result = spawnSync('python3', [verifier], {encoding: 'utf8'});
    assert.equal(result.status, 0, result.stderr || result.stdout);
    assert.match(result.stdout, /PASS native Mail product contracts/);
    assert.equal(FIXTURE.accounts.length, 2);
});
