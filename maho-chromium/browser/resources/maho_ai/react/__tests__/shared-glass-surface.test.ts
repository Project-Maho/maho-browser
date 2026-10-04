import {readFileSync} from 'node:fs';
import path from 'node:path';
import {describe, expect, it} from 'vitest';

describe('desktop embedded neutral glass surface', () => {
  it('separates the AI page material from the sidebar palette', () => {
    const html = readFileSync(
        path.join(process.cwd(), 'maho_ai.html'), 'utf8');
    const compactShell = readFileSync(
        path.join(process.cwd(), 'react/features/compact/compact-shell.tsx'),
        'utf8');
    const app = readFileSync(
        path.join(process.cwd(), 'react/app.tsx'), 'utf8');
    const composer = readFileSync(
        path.join(process.cwd(), 'react/features/compact/composer.tsx'), 'utf8');
    const nativeHost = readFileSync(
        path.join(process.cwd(), '../../ui/views/side_panel/maho_ai_side_panel_web_view.cc'),
        'utf8');
    const chromiumOverrides = readFileSync(
        path.join(process.cwd(), '../../../build/scripts/apply_chromium_src_overrides.py'),
        'utf8');

    expect(html).toContain('html, body, #app');
    expect(html).toContain('background: transparent !important');
    expect(app).toContain('maho-ai-page-surface');
    expect(app).toContain('bg-background/90');
    expect(app).toContain('border-border');
    expect(app).toContain('backdrop-blur-2xl');
    expect(app).not.toContain('sticky bottom-0 z-10 bg-gradient-to-t from-background');
    expect(compactShell).not.toContain('gap-3 bg-background px-3');
    expect(composer).toMatch(/bg-(background|card|muted)/);
    expect(nativeHost).toContain('WebContentsSetBackgroundColor::CreateForWebContentsWithColor');
    expect(nativeHost).not.toContain('MahoSidebarPalette');
    expect(nativeHost).not.toContain('sidebar_palette_');
    expect(nativeHost).not.toContain('maho-shared-glass-style');
    expect(nativeHost).not.toContain('linear-gradient(135deg');
    expect(chromiumOverrides).toContain('ui/accelerated_widget_mac/ca_layer_tree_coordinator.mm');
    expect(chromiumOverrides).toContain('root_ca_layer_.opaque = NO');
  });
});
