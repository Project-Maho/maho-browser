import {useCallback, useLayoutEffect, useRef} from 'react';

import {
  captureEditableState,
  restoreEditableState,
  type EditableSnapshot,
} from '../lib/editable-snapshot.js';

interface ComposerTextareaProps {
  className?: string;
  onSubmit: () => void;
  onValueChange: (value: string) => void;
  placeholder: string;
  readOnly: boolean;
  rows: number;
  value: string;
}

export function ComposerTextarea(
    {
      className = 'compact-composer__input',
      onSubmit,
      onValueChange,
      placeholder,
      readOnly,
      rows,
      value,
    }: ComposerTextareaProps) {
  const textareaRef = useRef<HTMLTextAreaElement | null>(null);
  const compositionActiveRef = useRef(false);
  const pendingSnapshotRef = useRef<EditableSnapshot | null>(null);
  const pendingValueRef = useRef<string | null>(null);

  useLayoutEffect(() => {
    const textarea = textareaRef.current;
    if (!textarea || textarea.value === value) {
      return;
    }

    const restoreFocus = document.activeElement === textarea;
    const snapshot = captureEditableState(textarea);
    if (compositionActiveRef.current) {
      pendingSnapshotRef.current = snapshot;
      pendingValueRef.current = value;
      return;
    }

    textarea.value = value;
    restoreEditableState(textarea, snapshot, restoreFocus);
  }, [value]);

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
  }, []);

  return (
    <textarea
      ref={textareaRef}
      aria-label="Message Maho AI"
      className={className}
      defaultValue={value}
      disabled={readOnly}
      placeholder={placeholder}
      rows={rows}
      onCompositionEnd={event => {
        compositionActiveRef.current = false;
        onValueChange(event.currentTarget.value);
        queueMicrotask(commitPendingValue);
      }}
      onCompositionStart={() => {
        compositionActiveRef.current = true;
      }}
      onInput={event => onValueChange(event.currentTarget.value)}
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
