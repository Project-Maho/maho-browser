const { remote } = require('webdriverio');
const fs = require('fs');
const path = require('path');
const zlib = require('zlib');

const UDID = 'R3CN8126R4Y';
const APP_PACKAGE = 'dev.maho.browser';
const EVIDENCE_DIR = path.join(__dirname, '..', '..', '..', '.omo', 'evidence', 'real-use-android');
const results = [];

// The card lays out an 11dp inset, then a 144dp preview box, then its labels;
// this band isolates the preview so the labels cannot fake a match.
const PREVIEW_BAND = { top: 0.06, bottom: 0.72 };

function record(id, status, detail) {
  results.push({ id, status, detail });
  console.log(`[${status}] ${id} :: ${detail}`);
}

function writePng(name, base64) {
  fs.mkdirSync(EVIDENCE_DIR, { recursive: true });
  const file = path.join(EVIDENCE_DIR, `${name}.png`);
  fs.writeFileSync(file, Buffer.from(base64, 'base64'));
  return file;
}

function paeth(a, b, c) {
  const p = a + b - c;
  const pa = Math.abs(p - a);
  const pb = Math.abs(p - b);
  const pc = Math.abs(p - c);
  if (pa <= pb && pa <= pc) return a;
  return pb <= pc ? b : c;
}

function decodePng(buffer) {
  let offset = 8;
  let width = 0;
  let height = 0;
  let channels = 4;
  const idat = [];
  while (offset < buffer.length) {
    const length = buffer.readUInt32BE(offset);
    const type = buffer.toString('ascii', offset + 4, offset + 8);
    const data = buffer.subarray(offset + 8, offset + 8 + length);
    if (type === 'IHDR') {
      width = data.readUInt32BE(0);
      height = data.readUInt32BE(4);
      const colorType = data.readUInt8(9);
      if (data.readUInt8(8) !== 8 || data.readUInt8(12) !== 0) {
        throw new Error('only 8-bit non-interlaced PNG screenshots are supported');
      }
      channels = { 0: 1, 2: 3, 4: 2, 6: 4 }[colorType];
      if (!channels) throw new Error(`unsupported PNG colour type ${colorType}`);
    } else if (type === 'IDAT') {
      idat.push(data);
    } else if (type === 'IEND') {
      break;
    }
    offset += 12 + length;
  }

  const raw = zlib.inflateSync(Buffer.concat(idat));
  const stride = width * channels;
  const pixels = Buffer.alloc(height * stride);
  for (let y = 0; y < height; y += 1) {
    const filter = raw[y * (stride + 1)];
    const line = raw.subarray(y * (stride + 1) + 1, y * (stride + 1) + 1 + stride);
    for (let x = 0; x < stride; x += 1) {
      const left = x >= channels ? pixels[y * stride + x - channels] : 0;
      const up = y > 0 ? pixels[(y - 1) * stride + x] : 0;
      const upLeft = y > 0 && x >= channels ? pixels[(y - 1) * stride + x - channels] : 0;
      let value = line[x];
      if (filter === 1) value += left;
      else if (filter === 2) value += up;
      else if (filter === 3) value += Math.floor((left + up) / 2);
      else if (filter === 4) value += paeth(left, up, upLeft);
      pixels[y * stride + x] = value & 0xff;
    }
  }
  return { width, height, channels, pixels };
}

// Reduce an image to a small grayscale signature so two captures of the same
// document compare equal regardless of scale, and a placeholder does not.
function signature(base64, size = 16, band = null) {
  const png = decodePng(Buffer.from(base64, 'base64'));
  const cells = new Array(size * size).fill(0);
  const counts = new Array(size * size).fill(0);
  const top = band ? Math.round(band.top * png.height) : 0;
  const bottom = band ? Math.round(band.bottom * png.height) : png.height;
  for (let y = top; y < bottom; y += 1) {
    const row = Math.min(size - 1, Math.floor(((y - top) / (bottom - top)) * size));
    for (let x = 0; x < png.width; x += 1) {
      const col = Math.min(size - 1, Math.floor((x / png.width) * size));
      const i = (png.width * y + x) * png.channels;
      const luma = png.channels >= 3
        ? 0.299 * png.pixels[i] + 0.587 * png.pixels[i + 1] + 0.114 * png.pixels[i + 2]
        : png.pixels[i];
      cells[row * size + col] += luma;
      counts[row * size + col] += 1;
    }
  }
  return cells.map((sum, i) => (counts[i] ? sum / counts[i] : 0));
}

function stdDev(values) {
  const mean = values.reduce((a, b) => a + b, 0) / values.length;
  return Math.sqrt(values.reduce((a, b) => a + (b - mean) ** 2, 0) / values.length);
}

function correlation(a, b) {
  const meanA = a.reduce((x, y) => x + y, 0) / a.length;
  const meanB = b.reduce((x, y) => x + y, 0) / b.length;
  let num = 0;
  let da = 0;
  let db = 0;
  for (let i = 0; i < a.length; i += 1) {
    const x = a[i] - meanA;
    const y = b[i] - meanB;
    num += x * y;
    da += x * x;
    db += y * y;
  }
  return da && db ? num / Math.sqrt(da * db) : 0;
}

async function visible(driver, selector) {
  try {
    const el = await driver.$(selector);
    return (await el.isExisting()) && (await el.isDisplayed());
  } catch (_) { return false; }
}

async function until(driver, description, condition, timeout = 45000) {
  return driver.waitUntil(condition, { timeout, interval: 500, timeoutMsg: `timed out waiting for: ${description}` });
}

const tag = (t) => `android=new UiSelector().resourceIdMatches(".*${t}")`;
const desc = (d) => `android=new UiSelector().description("${d}")`;

async function run() {
  const driver = await remote({
    hostname: '127.0.0.1', port: 4723, logLevel: 'error',
    capabilities: {
      platformName: 'Android', 'appium:automationName': 'UiAutomator2', 'appium:udid': UDID,
      'appium:appPackage': APP_PACKAGE, 'appium:appActivity': '.MainActivity',
      'appium:noReset': true, 'appium:fullReset': false,
      'appium:autoGrantPermissions': false, 'appium:newCommandTimeout': 300,
    },
  });

  try {
    await until(driver, 'shell root', () => visible(driver, tag('androidShellRoot')), 60000);

    for (const candidate of [desc('New Tab'), tag('arcBottomBarPageButton')]) {
      const el = await driver.$(candidate);
      if (await el.isExisting()) { await el.click(); break; }
    }
    await until(driver, 'url field', () => visible(driver, 'android=new UiSelector().className("android.widget.EditText")'), 15000);
    const field = await driver.$('android=new UiSelector().className("android.widget.EditText")');
    await field.click();
    await field.setValue('https://example.com');
    await driver.pressKeyCode(66);
    await until(driver, 'page rendered', async () => (await driver.getPageSource()).includes('Example Domain'), 45000);
    await driver.pause(1500);

    const pageShot = await driver.takeScreenshot();
    writePng('c5-page-reference', pageShot);
    const pageSignature = signature(pageShot);

    const tabsButton = await driver.$(desc('Tabs'));
    await tabsButton.click();
    await until(driver, 'tab grid', async () => (await driver.getPageSource()).includes('Tab deck'), 20000);
    await until(driver, 'tab card', () => visible(driver, tag('tabCard_0')), 20000);
    await driver.pause(1200);
    writePng('c5-tab-grid-preview', await driver.takeScreenshot());

    const previewEl = await driver.$(tag('tabCard_0'));
    const cardShot = await previewEl.takeElementScreenshot();
    const file = writePng('c5-tab-card-preview-region', cardShot);
    const previewSignature = signature(cardShot, 16, PREVIEW_BAND);

    const spread = stdDev(previewSignature);
    const match = correlation(previewSignature, pageSignature);

    if (spread < 2) {
      record('C5-tab-preview-rendered', 'FAIL',
        `preview region is flat (stdDev ${spread.toFixed(2)}), so no page thumbnail was drawn: ${file}`);
    } else if (match < 0.5) {
      record('C5-tab-preview-rendered', 'FAIL',
        `preview region does not match the open document (correlation ${match.toFixed(2)}): ${file}`);
    } else {
      record('C5-tab-preview-rendered', 'PASS',
        `preview shows the open document (correlation ${match.toFixed(2)}, stdDev ${spread.toFixed(2)}): ${file}`);
    }
  } finally {
    fs.mkdirSync(EVIDENCE_DIR, { recursive: true });
    fs.writeFileSync(path.join(EVIDENCE_DIR, 'c5-results.json'), JSON.stringify(results, null, 2));
    await driver.deleteSession().catch(() => {});
  }
}

run().then(() => {
  const failed = results.filter((r) => r.status !== 'PASS');
  console.log(`RESULT ${results.length - failed.length}/${results.length} passed`);
  process.exit(failed.length === 0 && results.length > 0 ? 0 : 1);
}, (e) => { console.error('RUN ERROR', e && e.message ? e.message : e); process.exit(2); });
