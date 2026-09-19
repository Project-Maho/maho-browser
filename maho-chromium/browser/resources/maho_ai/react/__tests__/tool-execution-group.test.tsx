import React, {act} from 'react';
import {createRoot, type Root} from 'react-dom/client';
import {afterEach, beforeEach, describe, expect, it} from 'vitest';
import {ToolExecutionGroup, ToolStep} from '../components/tool-execution-group.js';

Object.defineProperty(globalThis, 'IS_REACT_ACT_ENVIRONMENT', {
  configurable: true,
  value: true,
});

describe('ToolExecutionGroup — Aside parity collapsible tool grouping', () => {
  let container: HTMLDivElement;
  let root: Root;

  beforeEach(() => {
    container = document.createElement('div');
    document.body.appendChild(container);
    root = createRoot(container);
  });

  afterEach(() => {
    act(() => root.unmount());
    container.remove();
  });

  const mockSteps: ToolStep[] = [
    {
      key: 'step-1',
      text: 'Checking yt-dlp and ffmpeg',
      isError: false,
      toolName: 'bash',
      timestamp: 1000,
    },
    {
      key: 'step-2',
      text: 'Listing playlist tracks',
      isError: false,
      toolName: 'bash',
      timestamp: 3000,
    },
    {
      key: 'step-3',
      text: 'Checking mutagen',
      isError: true,
      toolName: 'bash',
      timestamp: 5000,
    },
  ];

  it('renders duration header, category, and steps matching reference layout', () => {
    act(() => {
      root.render(
        <ToolExecutionGroup
          steps={mockSteps}
          startTime={1000}
          endTime={5000}
          isRunning={false}
        />
      );
    });

    expect(container.textContent).toContain('Worked for 4s');
    expect(container.textContent).toContain('Ran commands');
    expect(container.textContent).toContain('Checking yt-dlp and ffmpeg');
    expect(container.textContent).toContain('Listing playlist tracks');
    expect(container.textContent).toContain('Checking mutagen');
    expect(container.textContent).toContain('Error');
  });

  it('toggles overall expansion when the duration header is clicked', () => {
    act(() => {
      root.render(
        <ToolExecutionGroup
          steps={mockSteps}
          startTime={1000}
          endTime={7000}
          isRunning={false}
        />
      );
    });

    const headerButton = container.querySelector('.group\\/header') as HTMLButtonElement;
    expect(headerButton).not.toBeNull();
    expect(headerButton.getAttribute('aria-expanded')).toBe('true');
    expect(container.textContent).toContain('Ran commands');

    act(() => {
      headerButton.click();
    });
    expect(headerButton.getAttribute('aria-expanded')).toBe('false');
    expect(container.textContent).not.toContain('Ran commands');
    expect(container.textContent).not.toContain('Checking yt-dlp and ffmpeg');

    act(() => {
      headerButton.click();
    });
    expect(headerButton.getAttribute('aria-expanded')).toBe('true');
    expect(container.textContent).toContain('Ran commands');
    expect(container.textContent).toContain('Checking yt-dlp and ffmpeg');
  });

  it('toggles command list when the sub-category button is clicked', () => {
    act(() => {
      root.render(
        <ToolExecutionGroup
          steps={mockSteps}
          startTime={1000}
          endTime={60000}
          isRunning={false}
        />
      );
    });

    const subButton = container.querySelector('.group\\/sub') as HTMLButtonElement;
    expect(subButton).not.toBeNull();
    expect(subButton.getAttribute('aria-expanded')).toBe('true');
    expect(container.textContent).toContain('Checking yt-dlp and ffmpeg');

    act(() => {
      subButton.click();
    });
    expect(subButton.getAttribute('aria-expanded')).toBe('false');
    expect(container.textContent).not.toContain('Checking yt-dlp and ffmpeg');

    act(() => {
      subButton.click();
    });
    expect(subButton.getAttribute('aria-expanded')).toBe('true');
    expect(container.textContent).toContain('Checking yt-dlp and ffmpeg');
  });

  it('renders tool step error detail when a step has isError and errorDetail', () => {
    const stepsWithError: ToolStep[] = [
      {
        key: 'step-err',
        text: 'Executing bash command',
        isError: true,
        toolName: 'bash',
        errorDetail: 'Command failed with exit code 127: mutagen: command not found',
        timestamp: 1000,
      },
    ];

    act(() => {
      root.render(
        <ToolExecutionGroup
          steps={stepsWithError}
          startTime={1000}
          endTime={2000}
          isRunning={false}
        />
      );
    });

    const errorDetailEl = container.querySelector('[data-testid="tool-step-error-detail"]');
    expect(errorDetailEl).not.toBeNull();
    expect(errorDetailEl?.textContent).toContain('Command failed with exit code 127: mutagen: command not found');
  });
});
