import {describe, it, expect} from 'vitest';
import {renderTrustedMarkdown} from '../../views/cards.js';

describe('renderTrustedMarkdown', () => {
  it('renders normal markdown successfully', () => {
    const result = renderTrustedMarkdown('Hello **world**');
    expect(String(result)).toContain('<strong>world</strong>');
  });

  it('generates links with target and rel attributes', () => {
    const result = renderTrustedMarkdown('[Google](https://google.com)');
    expect(String(result)).toContain('target="_blank"');
    expect(String(result)).toContain('rel="noopener noreferrer"');
  });

  it('catches sanitizer exceptions and uses plain-text fallback', () => {
    const result = renderTrustedMarkdown('Some CRASH_PRIMARY text');
    expect(String(result)).toContain('⚠️ 렌더 실패: 안전하지 않은 마크업이 포함되었습니다.');
    expect(String(result)).toContain('Some CRASH_PRIMARY text');
  });

  it('returns the short error sentence when secondary fallback fails', () => {
    const result = renderTrustedMarkdown('CRASH_SECONDARY');
    expect(String(result)).toBe('[Maho AI] 렌더링 오류가 발생했습니다.');
  });

  it('returns a plain string (not empty) when all sanitize calls throw', () => {
    const result = renderTrustedMarkdown('CRASH_ALL input');
    expect(typeof result).toBe('string');
    expect(result as unknown as string)
        .toBe('[Maho AI] 렌더링 오류가 발생했습니다.');
  });
});
