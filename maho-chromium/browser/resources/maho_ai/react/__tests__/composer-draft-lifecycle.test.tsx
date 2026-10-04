import {afterEach, describe, expect, it, vi} from 'vitest';

import {bindComposerDraftLifecycle} from '../features/compact/composer-draft-persistence.js';

describe('composer draft lifecycle events', () => {
  afterEach(() => {
    vi.restoreAllMocks();
  });

  it('flushes on hidden visibility changes and refreshes on visible changes', () => {
    // Given
    let visibilityState: DocumentVisibilityState = 'visible';
    vi.spyOn(document, 'visibilityState', 'get')
        .mockImplementation(() => visibilityState);
    const flush = vi.fn();
    const refresh = vi.fn();
    const unbind = bindComposerDraftLifecycle(document, flush, refresh);

    // When
    visibilityState = 'hidden';
    document.dispatchEvent(new Event('visibilitychange'));

    // Then
    expect(flush).toHaveBeenCalledOnce();
    expect(refresh).not.toHaveBeenCalled();

    // When
    visibilityState = 'visible';
    document.dispatchEvent(new Event('visibilitychange'));

    // Then
    expect(flush).toHaveBeenCalledOnce();
    expect(refresh).toHaveBeenCalledOnce();

    unbind();
    document.dispatchEvent(new Event('visibilitychange'));
    expect(refresh).toHaveBeenCalledOnce();
  });
});
