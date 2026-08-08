import { Resvg } from '@resvg/resvg-js';
import { html } from 'satori-html';
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { existsSync, mkdirSync, readdirSync, readFileSync, rmSync, writeFileSync } from 'node:fs';
import satori from 'satori';

import { ogConfigs, ogSize } from '../og.config.mjs';

const __dirname = dirname(fileURLToPath(import.meta.url));
const projectRoot = join(__dirname, '..');
const outDir = join(projectRoot, 'public', 'og');
const require = createRequire(import.meta.url);

const MAX_PNG_BYTES = 300 * 1024;

const accentPalette = {
  browser: '#8b7ff5',
  mail: '#5fb3c7',
  neutral: '#8b7ff5',
};

const accentCategories = {
  browser: 'BROWSER',
  mail: 'MAIL',
  neutral: '',
};

const logoDataUri = `data:image/png;base64,${readFileSync(join(projectRoot, 'public', 'icon-512.png')).toString('base64')}`;

function alpha(hex, opacity) {
  const normalized = hex.replace('#', '');
  const value = normalized.length === 3
    ? normalized.split('').map((chunk) => chunk + chunk).join('')
    : normalized;

  const numeric = Number.parseInt(value, 16);
  const red = (numeric >> 16) & 255;
  const green = (numeric >> 8) & 255;
  const blue = numeric & 255;

  return `rgba(${red}, ${green}, ${blue}, ${opacity})`;
}

function resolveFontFile(packageName, matcher) {
  const packageJsonPath = require.resolve(`${packageName}/package.json`);
  const filesDirectory = join(dirname(packageJsonPath), 'files');
  const match = readdirSync(filesDirectory).find((file) => matcher.test(file));

  if (!match) {
    throw new Error(`Could not find a font in ${filesDirectory} matching ${String(matcher)}`);
  }

  return join(filesDirectory, match);
}

const fonts = [
  {
    name: 'Instrument Serif',
    data: readFileSync(resolveFontFile('@fontsource/instrument-serif', /instrument-serif-latin-400-normal\.woff$/)),
    weight: 400,
    style: 'normal',
  },
  {
    name: 'Geist',
    data: readFileSync(resolveFontFile('@fontsource/geist', /geist-latin-400-normal\.woff$/)),
    weight: 400,
    style: 'normal',
  },
  {
    name: 'Geist',
    data: readFileSync(resolveFontFile('@fontsource/geist', /geist-latin-500-normal\.woff$/)),
    weight: 500,
    style: 'normal',
  },
  {
    name: 'Geist Mono',
    data: readFileSync(resolveFontFile('@fontsource/geist-mono', /geist-mono-latin-400-normal\.woff$/)),
    weight: 400,
    style: 'normal',
  },
];

function titleSizeFor(title) {
  const length = title.length;
  if (length <= 6) return 140;
  if (length <= 12) return 108;
  return 92;
}

async function renderOne(config) {
  const accentColor = accentPalette[config.accent] ?? accentPalette.neutral;
  const category = accentCategories[config.accent] ?? '';
  const titleSize = titleSizeFor(config.title);

  const footerRight = category
    ? `<div style="display: flex; align-items: center; gap: 12px;"><div style="display: flex; width: 9px; height: 9px; border-radius: 999px; background: ${accentColor};"></div><span style="font-family: 'Geist Mono'; font-size: 19px; letter-spacing: 0.22em; color: ${alpha(accentColor, 0.92)};">${category}</span></div>`
    : '<div style="display: flex;"></div>';

  const markup = html([
    `<div style="width: 100%; height: 100%; display: flex; position: relative; overflow: hidden; background: #0a0a0b; color: #fafafa; font-family: 'Geist';">`,
    // single subtle single-hue glow, top-right, fading to transparent
    `<div style="display: flex; position: absolute; top: -260px; right: -180px; width: 760px; height: 760px; border-radius: 999px; background: radial-gradient(circle, ${alpha(accentColor, 0.22)} 0%, ${alpha(accentColor, 0)} 68%);"></div>`,
    `<div style="position: relative; display: flex; flex-direction: column; justify-content: space-between; width: 100%; height: 100%; padding: 76px;">`,
    // brand lockup — the single brand mark
    `<div style="display: flex; align-items: center; gap: 20px;">`,
    `<img src="${logoDataUri}" style="width: 60px; height: 60px; border-radius: 15px;" />`,
    `<span style="font-family: 'Geist'; font-weight: 500; font-size: 37px; letter-spacing: -0.01em; color: #fafafa;">Maho</span>`,
    `</div>`,
    // title + subtitle
    `<div style="display: flex; flex-direction: column;">`,
    `<h1 style="margin: 0; max-width: 940px; font-family: 'Instrument Serif'; font-size: ${titleSize}px; line-height: 1.0; letter-spacing: -0.025em; color: #fafafa;">${config.title}</h1>`,
    `<p style="margin: 30px 0 0 0; max-width: 830px; font-size: 31px; line-height: 1.42; color: #93939c;">${config.subtitle}</p>`,
    `</div>`,
    // footer: url + product category
    `<div style="display: flex; align-items: center; justify-content: space-between;">`,
    `<span style="font-family: 'Geist Mono'; font-size: 22px; letter-spacing: 0.01em; color: #63636b;">mahobrowser.com</span>`,
    footerRight,
    `</div>`,
    `</div>`,
    `</div>`,
  ].join(''));

  const svg = await satori(markup, {
    width: ogSize.width,
    height: ogSize.height,
    fonts,
  });

  const resvg = new Resvg(svg);
  const pngData = resvg.render().asPng();

  if (pngData.length > MAX_PNG_BYTES) {
    throw new Error(`OG image for ${config.slug} exceeded 300KB (${(pngData.length / 1024).toFixed(1)} KB)`);
  }

  writeFileSync(join(outDir, `${config.slug}.png`), pngData);
  return pngData.length;
}

async function main() {
  const start = Date.now();

  if (existsSync(outDir)) {
    rmSync(outDir, { recursive: true, force: true });
  }
  mkdirSync(outDir, { recursive: true });

  console.log(`Generating ${ogConfigs.length} OG images into public/og ...`);

  let totalBytes = 0;

  for (const config of ogConfigs) {
    const size = await renderOne(config);
    totalBytes += size;
    console.log(`  ✓ og/${config.slug}.png (${(size / 1024).toFixed(1)} KB)`);
  }

  console.log(
    `Generated ${ogConfigs.length} OG images (${(totalBytes / 1024).toFixed(1)} KB total) in ${Date.now() - start}ms`,
  );
}

main().catch((error) => {
  console.error(error);
  process.exitCode = 1;
});
