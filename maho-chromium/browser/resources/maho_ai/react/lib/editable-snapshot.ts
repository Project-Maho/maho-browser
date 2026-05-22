export interface EditableSnapshot {
  selectionEnd: number;
  selectionStart: number;
  scrollTop: number;
}

export function captureEditableState(editable: HTMLTextAreaElement): EditableSnapshot {
  return {
    selectionEnd: editable.selectionEnd ?? editable.value.length,
    selectionStart: editable.selectionStart ?? editable.value.length,
    scrollTop: editable.scrollTop,
  };
}

export function restoreEditableState(
    editable: HTMLTextAreaElement,
    snapshot: EditableSnapshot | null,
    restoreFocus: boolean): void {
  if (!snapshot) {
    return;
  }

  const selectionStart = Math.min(snapshot.selectionStart, editable.value.length);
  const selectionEnd = Math.min(snapshot.selectionEnd, editable.value.length);

  if (restoreFocus) {
    editable.focus({preventScroll: true});
  }

  if (restoreFocus || document.activeElement === editable) {
    editable.setSelectionRange(selectionStart, selectionEnd);
  } else {
    editable.selectionStart = selectionStart;
    editable.selectionEnd = selectionEnd;
  }

  editable.scrollTop = snapshot.scrollTop;
}
