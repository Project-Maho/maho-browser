import {readFileSync} from 'node:fs';
import path from 'node:path';
import {describe, expect, it} from 'vitest';

const composerSourcePath = path.join(
    process.cwd(), 'react/features/compact/composer.tsx');

describe('Composer context picker trigger', () => {
  it('forwards Popover trigger props to the actual add-context button', () => {
    const source = readFileSync(composerSourcePath, 'utf8');

    expect(source).toMatch(
        /<Tooltip>\s*<TooltipTrigger asChild>\s*<PopoverTrigger asChild>\s*<Button\s+aria-label="Add context or start a new session"/);
    expect(source).not.toContain(`<PopoverTrigger asChild>
            <Tooltip>`);
  });
});
