import * as React from 'react';
import {useCallback, useEffect, useLayoutEffect, useRef} from 'react';

import {
  captureEditableState,
  restoreEditableState,
  type EditableSnapshot,
} from '../lib/editable-snapshot.js';
import { Textarea } from '@ui/textarea';

interface ComposerTextareaProps {
  className?: string;
  focusRequest: number;
  onBlur?: () => void;
  onPasteFiles?: (files: File[]) => void;
  onSubmit: () => void;
  onValueChange: (value: string) => void;
  placeholder: string;
  readOnly: boolean;
  rows: number;
  value: string;
}

export function ComposerTextarea(
    {
      className,
      focusRequest,
      onBlur,
      onPasteFiles,
      onSubmit,
      onValueChange,
      placeholder,
      readOnly,
      rows,
      value,
    }: ComposerTextareaProps) {
  const textareaRef = useRef<HTMLTextAreaElement | null>(null);
  const lastFocusRequestRef = useRef(focusRequest);
  const compositionActiveRef = useRef(false);
  const pendingSnapshotRef = useRef<EditableSnapshot | null>(null);
  const pendingValueRef = useRef<string | null>(null);

  const adjustHeight = useCallback(() => {
    const textarea = textareaRef.current;
    if (!textarea) {
      return;
    }
    textarea.style.height = 'auto';
    if (textarea.scrollHeight > 0) {
      textarea.style.height = `${textarea.scrollHeight}px`;
    }
  }, []);

  useLayoutEffect(() => {
    if (lastFocusRequestRef.current === focusRequest) {
      return;
    }

    lastFocusRequestRef.current = focusRequest;
    textareaRef.current?.focus({preventScroll: true});
  }, [focusRequest]);

  useLayoutEffect(() => {
    const textarea = textareaRef.current;
    if (!textarea) {
      return;
    }

    if (textarea.value !== value) {
      const restoreFocus = document.activeElement === textarea;
      const snapshot = captureEditableState(textarea);
      if (compositionActiveRef.current) {
        pendingSnapshotRef.current = snapshot;
        pendingValueRef.current = value;
        return;
      }

      textarea.value = value;
      restoreEditableState(textarea, snapshot, restoreFocus);
    }
    adjustHeight();
  }, [value, adjustHeight]);

  useEffect(() => {
    const textarea = textareaRef.current;
    if (!textarea || typeof ResizeObserver === 'undefined') {
      return;
    }
    const parent = textarea.parentElement;
    if (!parent) {
      return;
    }
    let lastWidth = parent.clientWidth;
    const observer = new ResizeObserver(entries => {
      for (const entry of entries) {
        if (entry.contentRect.width !== lastWidth) {
          lastWidth = entry.contentRect.width;
          adjustHeight();
        }
      }
    });
    observer.observe(parent);
    return () => observer.disconnect();
  }, [adjustHeight]);

  const commitPendingValue = useCallback(() => {
    const textarea = textareaRef.current;
    if (!textarea || pendingValueRef.current === null) {
      return;
    }

    const restoreFocus = document.activeElement === textarea;
    textarea.value = pendingValueRef.current;
    restoreEditableState(textarea, pendingSnapshotRef.current, restoreFocus);
    pendingSnapshotRef.current = null;
    pendingValueRef.current = null;
    adjustHeight();
  }, [adjustHeight]);

  return (
    <Textarea
      ref={textareaRef}
      aria-label="Message Maho AI"
      className={className}
      defaultValue={value}
      disabled={readOnly}
      placeholder={placeholder}
      rows={rows}
      onBlur={onBlur}
      onCompositionEnd={event => {
        compositionActiveRef.current = false;
        adjustHeight();
        onValueChange(event.currentTarget.value);
        queueMicrotask(commitPendingValue);
      }}
      onCompositionStart={() => {
        compositionActiveRef.current = true;
      }}
      onInput={event => {
        adjustHeight();
        onValueChange(event.currentTarget.value);
      }}
      onPaste={event => {
        const files = Array.from(event.clipboardData?.files || []);
        if (!files.length || !onPasteFiles) {
          return;
        }

        event.preventDefault();
        onPasteFiles(files);
      }}
      onKeyDown={event => {
        if (readOnly) {
          return;
        }

        const nativeEvent = event.nativeEvent;
        if (nativeEvent.isComposing || nativeEvent.keyCode === 229) {
          return;
        }

        if (event.key === 'Enter' && !event.shiftKey) {
          event.preventDefault();
          onSubmit();
        }
      }}
    />
  );
}
