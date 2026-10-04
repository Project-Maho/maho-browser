const http = require('http');

function createFixtureServer(port = 0) {
    const server = http.createServer((req, res) => {
        if (req.url === '/downloads/regular.bin') {
            res.writeHead(200, { 'Content-Type': 'application/octet-stream' });
            res.end("REGULAR_DOWNLOAD_SECRET\n");
        } else if (req.url === '/downloads/private.bin') {
            res.writeHead(200, { 'Content-Type': 'application/octet-stream' });
            res.end("OTR_DOWNLOAD_SECRET\n");
        } else if (req.url === '/state/') {
            res.writeHead(200, { 'Content-Type': 'application/json' });
            res.end(JSON.stringify({ otr_session: "active", otr_local: "active" }));
        } else if (req.url === '/ai-site/') {
            res.writeHead(200, { 'Content-Type': 'text/html' });
            res.end("<html><head><title>AI Site</title></head><body><div id='ai-control'>Normal Control</div></body></html>");
        } else {
            res.writeHead(404);
            res.end();
        }
    });

    return new Promise((resolve) => {
        server.listen(port, '127.0.0.1', () => {
            resolve(server);
        });
    });
}

module.exports = { createFixtureServer };
