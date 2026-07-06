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
  neutral: '#a0a0a0',
};

const accentLabels = {
  browser: 'MAHO BROWSER',
  mail: 'MAHO MAIL',
  neutral: 'MAHO',
};

const titleSizes = [128, 104, 110, 96, 96, 102, 100, 96, 102];
const titleWidths = [620, 760, 780, 820, 840, 760, 760, 680, 820];
const subtitleWidths = [760, 760, 810, 760, 780, 760, 740, 700, 780];

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

function toKebabCase(property) {
  return property.replace(/[A-Z]/g, (match) => `-${match.toLowerCase()}`);
}

function styleToString(styleObject) {
  return Object.entries(styleObject)
    .filter(([, value]) => value !== undefined && value !== null)
    .map(([property, value]) => `${toKebabCase(property)}: ${value}`)
    .join('; ');
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

function getVariant(index, accentColor) {
  const haloTop = [30, 44, 22, 64, 18, 42, 28, 56, 16][index];
  const haloRight = [42, 112, 60, 148, 86, 132, 34, 168, 92][index];
  const haloSize = [360, 300, 420, 320, 390, 340, 370, 280, 410][index];
  const frameTop = [74, 94, 78, 110, 60, 100, 88, 118, 64][index];
  const frameRight = [92, 58, 176, 76, 212, 152, 102, 70, 190][index];
  const frameWidth = [230, 280, 220, 250, 180, 240, 270, 210, 200][index];
  const frameHeight = [230, 160, 280, 180, 260, 210, 160, 250, 290][index];
  const beamBottom = [146, 110, 120, 92, 84, 158, 132, 106, 76][index];
  const beamRight = [140, 86, 250, 138, 70, 214, 146, 232, 108][index];
  const beamWidth = [280, 220, 180, 260, 320, 210, 240, 170, 300][index];
  const topBandWidth = [250, 290, 210, 280, 220, 240, 300, 190, 260][index];
  const topBandLeft = [80, 160, 110, 210, 120, 250, 150, 230, 92][index];

  return {
    titleSize: titleSizes[index] ?? 96,
    titleWidth: titleWidths[index] ?? 780,
    subtitleWidth: subtitleWidths[index] ?? 780,
    halo: styleToString({
      display: 'flex',
      position: 'absolute',
      top: `${haloTop}px`,
      right: `${haloRight}px`,
      width: `${haloSize}px`,
      height: `${haloSize}px`,
      borderRadius: '999px',
      background: alpha(accentColor, 0.13),
      border: `1px solid ${alpha(accentColor, 0.18)}`,
      opacity: '1',
    }),
    frame: styleToString({
      display: 'flex',
      position: 'absolute',
      top: `${frameTop}px`,
      right: `${frameRight}px`,
      width: `${frameWidth}px`,
      height: `${frameHeight}px`,
      borderRadius: '38px',
      border: `1px solid ${alpha(accentColor, 0.46)}`,
      background: alpha(accentColor, 0.035),
    }),
    beam: styleToString({
      display: 'flex',
      position: 'absolute',
      bottom: `${beamBottom}px`,
      right: `${beamRight}px`,
      width: `${beamWidth}px`,
      height: '12px',
      borderRadius: '999px',
      background: `linear-gradient(90deg, ${alpha(accentColor, 0)} 0%, ${alpha(accentColor, 0.88)} 50%, ${alpha(accentColor, 0)} 100%)`,
    }),
    topBand: styleToString({
      display: 'flex',
      position: 'absolute',
      top: '82px',
      left: `${topBandLeft}px`,
      width: `${topBandWidth}px`,
      height: '1px',
      background: `linear-gradient(90deg, ${alpha(accentColor, 0)} 0%, ${alpha(accentColor, 0.52)} 40%, ${alpha(accentColor, 0)} 100%)`,
      opacity: '0.9',
    }),
  };
}

function getPageEyebrow(slug) {
  return slug.replace(/-/g, ' / ').toUpperCase();
}

async function renderOne(config, index) {
  const accentColor = accentPalette[config.accent] ?? accentPalette.neutral;
  const productLabel = accentLabels[config.accent] ?? accentLabels.neutral;
  const variant = getVariant(index, accentColor);
  const pageNumber = String(index + 1).padStart(2, '0');
  const markup = html([
    `<div style="width: 100%; height: 100%; display: flex; position: relative; overflow: hidden; background: linear-gradient(135deg, ${alpha(accentColor, 0.08)} 0%, #0a0a0a 28%, #0c0c0f 100%); color: #fafafa; font-family: 'Geist';">`,
    `<div style="display: flex; position: absolute; inset: 22px; border-radius: 34px; border: 1px solid ${alpha(accentColor, 0.14)};"></div>`,
    `<div style="display: flex; position: absolute; inset: 0; background: linear-gradient(180deg, ${alpha('#ffffff', 0.015)} 0%, transparent 22%, transparent 100%);"></div>`,
    `<div style="${variant.halo}"></div>`,
    `<div style="${variant.frame}"></div>`,
    `<div style="${variant.beam}"></div>`,
    `<div style="${variant.topBand}"></div>`,
    `<div style="position: relative; display: flex; flex-direction: column; width: 100%; height: 100%; padding: 80px;">`,
    `<div style="display: flex; align-items: center; justify-content: space-between; margin-bottom: 22px;">`,
    `<div style="display: flex; align-items: center; gap: 14px; padding: 14px 18px; border-radius: 999px; border: 1px solid ${alpha(accentColor, 0.18)}; background: ${alpha('#ffffff', 0.015)};">`,
    `<div style="display: flex; width: 10px; height: 10px; border-radius: 999px; background: ${accentColor};"></div>`,
    `<span style="font-family: 'Geist Mono'; font-size: 18px; letter-spacing: 0.16em; color: ${alpha('#ffffff', 0.92)}; text-transform: uppercase;">${getPageEyebrow(config.slug)}</span>`,
    `</div>`,
    `<span style="font-family: 'Geist Mono'; font-size: 16px; letter-spacing: 0.18em; text-transform: uppercase; color: ${alpha(accentColor, 0.84)};">OG ${pageNumber}/09</span>`,
    `</div>`,
    `<div style="flex: 1; display: flex; flex-direction: column; justify-content: center;">`,
    `<h1 style="margin: 0 0 30px 0; max-width: ${variant.titleWidth}px; font-family: 'Instrument Serif'; font-size: ${variant.titleSize}px; line-height: 1.05; letter-spacing: -0.04em; color: #fafafa;">${config.title}</h1>`,
    `<p style="margin: 0; max-width: ${variant.subtitleWidth}px; font-size: 32px; line-height: 1.36; color: #a0a0a0;">${config.subtitle}</p>`,
    `</div>`,
    `<div style="display: flex; align-items: flex-end; justify-content: space-between; gap: 32px;">`,
    `<span style="font-family: 'Geist Mono'; font-size: 24px; letter-spacing: 0.18em; text-transform: uppercase; color: #fafafa;">MAHO</span>`,
    `<div style="display: flex; align-items: center; gap: 14px; padding: 14px 20px; border-radius: 999px; border: 1px solid ${alpha(accentColor, 0.2)}; background: ${alpha(accentColor, 0.08)}; font-family: 'Geist Mono'; font-size: 20px; letter-spacing: 0.18em; text-transform: uppercase; color: ${accentColor};">`,
    `<div style="display: flex; width: 10px; height: 10px; border-radius: 999px; background: ${accentColor};"></div>`,
    `<span>${productLabel}</span>`,
    `</div>`,
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

  for (const [index, config] of ogConfigs.entries()) {
    const size = await renderOne(config, index);
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
