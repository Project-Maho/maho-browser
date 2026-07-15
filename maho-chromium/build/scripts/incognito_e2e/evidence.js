const fs = require('fs');
const path = require('path');

function saveEvidence(attemptDir, taskDir, filename, content) {
    const dir = path.join(attemptDir, taskDir);
    fs.mkdirSync(dir, { recursive: true });
    const filepath = path.join(dir, filename);
    fs.writeFileSync(filepath, JSON.stringify(content, null, 2));
    console.log(`Saved evidence to ${filepath}`);
}

module.exports = { saveEvidence };
