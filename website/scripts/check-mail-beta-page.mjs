// Structural gate for the Mail opt-in Beta presentation (machine-consumed policy check,
// not prose-pinning): asserts the built /mail page and homepage honor the locked contract.
// PASS/FAIL is decided by security/policy invariants, never by marketing wording.
import { readFileSync, existsSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';

const distDir = join(dirname(fileURLToPath(import.meta.url)), '..', 'dist');
const mailPath = join(distDir, 'mail', 'index.html');
const homePath = join(distDir, 'index.html');

const fails = [];
const read = (p) => (existsSync(p) ? readFileSync(p, 'utf8') : (fails.push(`missing build artifact: ${p}`), ''));
const mail = read(mailPath);
const home = read(homePath);

// Forbidden on /mail: privileged activation schemes + separate-app / absolute / unverifiable claims.
const forbidden = [
  [/chrome:\/\/maho/i, 'chrome://maho activation link'],
  [/maho:\/\//i, 'maho:// activation link'],
  [/download for macos/i, 'separate-app "Download for macOS" framing'],
  [/free[\s\u00a0-]*forever/i, 'absolute "Free Forever" claim'],
  [/100%\s*local/i, 'absolute "100% Local" claim'],
  [/security audit/i, 'unverifiable "Security Audit" claim'],
];
for (const [re, label] of forbidden) if (re.test(mail)) fails.push(`/mail contains forbidden ${label}`);

// Required on /mail: honest Beta label + Settings-only enable guidance.
if (!/beta/i.test(mail)) fails.push('/mail missing "Beta" label');
if (!/(enable[\s\S]{0,60}settings|settings[\s\S]{0,60}enable|settings[\s\S]{0,60}mail)/i.test(mail))
  fails.push('/mail missing Settings-enable guidance');

// Required on homepage: a Mail Beta card that links to /mail (plain navigation, no activate CTA).
if (!/href="\/mail"/.test(home)) fails.push('homepage missing link to /mail');
if (!/mail[\s\S]{0,160}beta/i.test(home)) fails.push('homepage missing "Mail · Beta" card label');
for (const [re, label] of [[/chrome:\/\/maho/i, 'chrome://maho'], [/maho:\/\//i, 'maho://']])
  if (re.test(home)) fails.push(`homepage contains forbidden ${label} link`);

if (fails.length) {
  console.error('GATE FAIL:\n- ' + fails.join('\n- '));
  process.exit(1);
}
console.log('GATE PASS: /mail + homepage satisfy the opt-in Beta structural contract');
