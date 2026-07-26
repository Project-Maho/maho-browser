// Copyright 2026 Maho Browser. All rights reserved.

import {readFileSync} from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {describe, expect, it} from 'vitest';

import {TOTAL_PAGES, WelcomePage} from '../types.js';

const REACT_DIR = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

describe('PasswordSetup isolated frontend contract', () => {
  it('is page 7 with six contiguous progress steps and no Completion progress', () => {
    const source = readFileSync(path.join(REACT_DIR, 'app.tsx'), 'utf8');

    expect(WelcomePage.PasswordSetup).toBe(7);
    expect(WelcomePage.Completion).toBe(8);
    expect(TOTAL_PAGES).toBe(9);
    expect(WelcomePage.PasswordSetup + 1).toBe(WelcomePage.Completion);
    expect(WelcomePage.PasswordSetup - WelcomePage.Appearance + 1).toBe(6);
    expect(WelcomePage.PasswordSetup - WelcomePage.Appearance).toBe(5);
    expect(source).toContain(
        'const WIZARD_STEPS = WelcomePage.PasswordSetup - WelcomePage.Appearance + 1;');
    expect(source).toContain(
        'const currentStep = pageForRender - WelcomePage.Appearance;');
    expect(source).toContain(
        'aria-label={`Setup progress: step ${current + 1} of ${total}`}');
    expect(source).toContain('{!hideSidebarChrome && !isCompletionPage && (');
    expect(source).toContain('<ul');
    expect(source).toContain('<li');
    expect(source).toContain("aria-current={i === current ? 'step' : undefined}");
  });

  it('blocks PasswordSetup generic keyboard, Skip, and footer navigation', () => {
    const source = readFileSync(path.join(REACT_DIR, 'app.tsx'), 'utf8');

    expect(source).toContain(
        'if (state.currentPage === WelcomePage.PasswordSetup) return;');
    expect(source).toContain('!isPasswordSetupPage && !isCompletionPage');
    expect(source).toContain('{!isPasswordSetupPage ? (');
  });

  it('keeps cards scrollable and the CTA fixed without clipping on mobile', () => {
    const appSource = readFileSync(path.join(REACT_DIR, 'app.tsx'), 'utf8');
    const pageSource = readFileSync(
        path.join(REACT_DIR, 'pages/password-setup.tsx'), 'utf8');
    const fieldsSource = readFileSync(
        path.join(REACT_DIR, 'pages/password-setup-fields.tsx'), 'utf8');

    expect(appSource).toContain(
        'max-md:[&>h1]:text-xl md:[&>h1]:text-2xl lg:[&>h1]:text-3xl');
    expect(pageSource).toContain('<h1 className="text-balance">');
    expect(pageSource).toContain('<p className="text-pretty">');
    expect(pageSource).toContain(
        'className="min-h-0 flex-1 space-y-4 overflow-y-auto pr-1 pb-4"');
    expect(pageSource).toContain(
        'className="shrink-0 border-t border-border pt-4"');
    expect(fieldsSource.match(
        /<CardDescription className="text-pretty text-xs leading-5">/g))
      .toHaveLength(2);
    expect(fieldsSource).toContain(
        'aria-describedby={selectedOption ? PROVIDER_DESCRIPTION_ID : undefined}');
    expect(fieldsSource.match(/<h2\b/g)).toHaveLength(2);
  });
});
