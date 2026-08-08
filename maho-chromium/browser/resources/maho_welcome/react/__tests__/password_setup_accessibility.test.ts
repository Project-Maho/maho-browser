// Copyright 2026 Maho Browser. All rights reserved.

import {readFileSync} from 'node:fs';
import path from 'node:path';
import {fileURLToPath} from 'node:url';
import {describe, expect, it} from 'vitest';

import {TOTAL_PAGES, WelcomePage} from '../types.js';

const REACT_DIR = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');

describe('PasswordSetup isolated frontend contract', () => {
  it('is page 8 followed by SyncKeyBackup, with eight user-facing progress steps (ImportProgress excluded)', () => {
    const source = readFileSync(path.join(REACT_DIR, 'app.tsx'), 'utf8');

    expect(WelcomePage.PasswordSetup).toBe(8);
    expect(WelcomePage.SyncKeyBackup).toBe(9);
    expect(WelcomePage.Completion).toBe(10);
    expect(TOTAL_PAGES).toBe(12);
    expect(WelcomePage.PasswordSetup + 1).toBe(WelcomePage.SyncKeyBackup);
    expect(WelcomePage.SyncKeyBackup + 1).toBe(WelcomePage.Completion);
    expect(source).toContain('const WIZARD_STEP_PAGES: readonly WelcomePage[] = [');
    expect(source).toContain('const WIZARD_STEPS = WIZARD_STEP_PAGES.length;');
    expect(source).toContain('const currentStep = WIZARD_STEP_PAGES.indexOf(stepIndicatorPage);');
    expect(source).not.toContain('WelcomePage.ImportProgress,\n  WelcomePage.SearchEngine');
    expect(source).toContain(
        'aria-label={`Setup progress: step ${current + 1} of ${total}`}');
    expect(source).toContain('{!hideSidebarChrome && !isCompletionPage && (');
    expect(source).toContain('<ul');
    expect(source).toContain('<li');
    expect(source).toContain("aria-current={i === current ? 'step' : undefined}");
  });

  it('blocks PasswordSetup and SyncKeyBackup generic keyboard, Skip, and footer navigation', () => {
    const source = readFileSync(path.join(REACT_DIR, 'app.tsx'), 'utf8');

    expect(source).toContain('state.currentPage === WelcomePage.PasswordSetup ||');
    expect(source).toContain('state.currentPage === WelcomePage.SyncKeyBackup) return;');
    expect(source).toContain('!isPasswordSetupPage && !isSyncKeyBackupPage && !isCompletionPage');
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
    expect(fieldsSource).toContain('role="radiogroup" aria-label="Select password manager provider"');
    expect(fieldsSource.match(/<h2\b/g)).toHaveLength(3);
  });
});
