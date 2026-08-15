import {readFileSync} from 'node:fs';
import {resolve} from 'node:path';

import {describe, expect, it} from 'vitest';

import {resolveMailBadgePresentation} from './components/mobile/BottomTabBar';

function source(relativePath: string) {
  return readFileSync(resolve(process.cwd(), relativePath), 'utf8');
}

function lifecycleStates(header: string) {
  const enumBody = header.match(/enum class LifecycleState\s*\{([\s\S]*?)\};/)?.[1];
  if (!enumBody) throw new Error('LifecycleState enum not found');
  return [...enumBody.matchAll(/\b(k[A-Z][A-Za-z0-9_]*)\b/g)].map(
    ([, state]) => state,
  );
}

function lifecycleEncoderCases(implementation: string) {
  const functionBody = implementation.match(
    /const char\* EncodeMailLifecycleState\([\s\S]*?\n\}/,
  )?.[0];
  if (!functionBody) throw new Error('EncodeMailLifecycleState not found');
  return [...functionBody.matchAll(/case LifecycleState::(k[A-Z][A-Za-z0-9_]*):/g)].map(
    ([, state]) => state,
  );
}

function nativeBadgePresentation(implementation: string, count: number) {
  const threshold = Number(
    implementation.match(/unread_count\s*>\s*(\d+)/)?.[1],
  );
  const cappedText = implementation.match(/\?\s*u"([^"]+)"/)?.[1];
  const baseName = implementation.match(/accessible_name\s*=\s*u"([^"]+)"/)?.[1];
  const singular = implementation.match(/unread_count\s*==\s*1\s*\?\s*u"([^"]+)"/)?.[1];
  const plural = implementation.match(/:\s*u"([^"]+)"\);/)?.[1];
  if (!threshold || !cappedText || !baseName || !singular || !plural) {
    throw new Error('Native badge presentation contract not found');
  }
  const text = count > threshold ? cappedText : String(count);
  return {
    text,
    accessibleName: `${baseName}, ${text}${count === 1 ? singular : plural}`,
  };
}

describe('native Mail contracts', () => {
  it('requires an explicit C++ encoder case for every lifecycle state', () => {
    const header = source('../../mail_helper/maho_mail_service.h');
    const implementation = source(
      '../../ui/webui/maho_mail/maho_mail_page_handler.cc',
    );

    expect(lifecycleEncoderCases(implementation)).toEqual(
      lifecycleStates(header),
    );
  });

  it.each([1, 99, 100])(
    'keeps the WebUI badge presentation equal to native for %i unread',
    (count) => {
      const nativeImplementation = source(
        '../../mail_helper/maho_mail_badge.cc',
      );
      expect(resolveMailBadgePresentation(count)).toEqual(
        nativeBadgePresentation(nativeImplementation, count),
      );
    },
  );
});
