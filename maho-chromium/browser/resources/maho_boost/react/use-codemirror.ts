import * as React from 'react';

interface CodeMirrorChangeSpec {
  readonly from: number;
  readonly insert: string;
  readonly to?: number;
}

interface CodeMirrorEditorView {
  readonly state: CodeMirrorEditorState;
  destroy(): void;
  dispatch(spec: {readonly changes: CodeMirrorChangeSpec}): void;
  focus(): void;
  requestMeasure(): void;
  setState(state: CodeMirrorEditorState): void;
}

interface CodeMirrorEditorState {readonly doc: {readonly length: number; toString(): string}}

interface CodeMirrorViewUpdate {
  readonly docChanged: boolean;
  readonly state: CodeMirrorEditorState;
}

type CodeMirrorExtension = unknown;

interface CodeMirrorFacet<Value> {of(value: Value): CodeMirrorExtension}

interface CodeMirrorApi {
  readonly EditorState: {
    create(config: {readonly doc: string; readonly extensions: CodeMirrorExtension[]}):
        CodeMirrorEditorState;
    readonly readOnly: CodeMirrorFacet<boolean>;
  };
  readonly EditorView: {
    new(config: {readonly parent: Element; readonly state: CodeMirrorEditorState}):
        CodeMirrorEditorView;
    readonly editable: CodeMirrorFacet<boolean>;
    readonly updateListener: {of(listener: (update: CodeMirrorViewUpdate) => void):
        CodeMirrorExtension};
  };
  readonly basicSetup: CodeMirrorExtension;
  css(): CodeMirrorExtension;
  readonly oneDark: CodeMirrorExtension;
}

interface CodeMirrorTestSnapshot {
  readonly css: string | null;
  readonly focused: boolean;
  readonly mounted: boolean;
}

interface MahoBoostTestApi {
  readonly flush: () => Promise<CodeMirrorTestSnapshot>;
  readonly focus: () => boolean;
  readonly measure: () => boolean;
  readonly mounted: () => boolean;
  readonly readCss: () => string | null;
  readonly writeCss: (css: string) => boolean;
}

declare global {
  interface Window {
    __mahoBoostTest?: MahoBoostTestApi;
    MahoCodeMirror?: CodeMirrorApi;
  }
}

export interface UseCodeMirrorOptions {
  readonly active: boolean;
  readonly disabled?: boolean;
  readonly focusRequest?: number;
  readonly onChange: (value: string) => void;
  readonly value: string;
}

export interface UseCodeMirrorResult {
  readonly appendText: (text: string) => void;
  readonly focus: () => void;
  readonly hostRef: React.RefObject<HTMLDivElement | null>;
  readonly mounted: boolean;
  readonly requestMeasure: () => void;
}

export function useCodeMirror(
    {active, disabled = false, focusRequest = 0, onChange, value}:
    UseCodeMirrorOptions): UseCodeMirrorResult {
  const hostRef = React.useRef<HTMLDivElement | null>(null);
  const viewRef = React.useRef<CodeMirrorEditorView | null>(null);
  const onChangeRef = React.useRef(onChange);
  const disabledRef = React.useRef(disabled);
  const valueRef = React.useRef(value);
  const syncingValueRef = React.useRef(false);
  const initialValueRef = React.useRef(value);
  const measureFrameRef = React.useRef<number | null>(null);
  const appliedDisabledRef = React.useRef<boolean | null>(null);
  const [activated, setActivated] = React.useState(active);
  const [mounted, setMounted] = React.useState(false);
  onChangeRef.current = onChange;
  disabledRef.current = disabled;
  valueRef.current = value;
  if (!viewRef.current) {
    initialValueRef.current = value;
  }

  const handleUpdate = React.useCallback((update: CodeMirrorViewUpdate): void => {
    if (update.docChanged && !syncingValueRef.current && !disabledRef.current) {
      onChangeRef.current(update.state.doc.toString());
    }
  }, []);
  const createState = React.useCallback(
      (api: CodeMirrorApi, doc: string, readOnly: boolean): CodeMirrorEditorState =>
        api.EditorState.create({
          doc,
          extensions: [
            api.basicSetup, api.css(), api.oneDark,
            api.EditorState.readOnly.of(readOnly),
            api.EditorView.editable.of(!readOnly),
            api.EditorView.updateListener.of(handleUpdate),
          ],
        }),
      [handleUpdate]);

  React.useEffect(() => {
    if (active) {
      setActivated(true);
    }
  }, [active]);

  React.useEffect(() => {
    if (!activated) {
      return;
    }
    const host = hostRef.current;
    if (!host) {
      return;
    }

    const api = window.MahoCodeMirror;
    if (!api) {
      return;
    }

    const state = createState(api, initialValueRef.current, disabledRef.current);
    const view = new api.EditorView({
      parent: host,
      state,
    });
    viewRef.current = view;
    appliedDisabledRef.current = disabledRef.current;
    setMounted(true);

    return () => {
      if (measureFrameRef.current !== null) {
        window.cancelAnimationFrame(measureFrameRef.current);
        measureFrameRef.current = null;
      }
      view.destroy();
      viewRef.current = null;
      appliedDisabledRef.current = null;
    };
  }, [activated, createState]);

  React.useLayoutEffect(() => {
    const view = viewRef.current;
    const api = window.MahoCodeMirror;
    if (!view || !api || appliedDisabledRef.current === disabled) {
      return;
    }
    view.setState(createState(api, valueRef.current, disabled));
    appliedDisabledRef.current = disabled;
    if (disabled && hostRef.current?.contains(document.activeElement) &&
        document.activeElement instanceof HTMLElement) {
      document.activeElement.blur();
    }
  }, [createState, disabled, mounted]);

  const writeValue = React.useCallback((nextValue: string, notify: boolean): boolean => {
    const view = viewRef.current;
    if (!view || (notify && disabledRef.current)) {
      return false;
    }
    if (view.state.doc.toString() === nextValue) {
      return true;
    }
    syncingValueRef.current = !notify;
    view.dispatch({
      changes: {from: 0, insert: nextValue, to: view.state.doc.length},
    });
    syncingValueRef.current = false;
    return true;
  }, []);

  React.useEffect(() => {
    writeValue(value, false);
  }, [mounted, value, writeValue]);

  const scheduleViewAction = React.useCallback((focus: boolean): boolean => {
    if (!viewRef.current || (focus && disabledRef.current)) {
      return false;
    }
    if (measureFrameRef.current !== null) {
      window.cancelAnimationFrame(measureFrameRef.current);
    }
    measureFrameRef.current = window.requestAnimationFrame(() => {
      measureFrameRef.current = null;
      viewRef.current?.requestMeasure();
      if (focus) {
        viewRef.current?.focus();
      }
    });
    return true;
  }, []);

  React.useEffect(() => {
    if (!active || !mounted || focusRequest <= 0) {
      return;
    }
    scheduleViewAction(true);
  }, [active, focusRequest, mounted, scheduleViewAction]);

  const flush = React.useCallback(async (): Promise<CodeMirrorTestSnapshot> => {
    await new Promise<void>(resolve => {
      window.requestAnimationFrame(() => resolve());
    });
    await Promise.resolve();
    return {
      css: viewRef.current?.state.doc.toString() ?? null,
      focused: hostRef.current?.contains(document.activeElement) ?? false,
      mounted: viewRef.current !== null,
    };
  }, []);

  React.useEffect(() => {
    if (!('domAutomationController' in window)) {
      return;
    }
    const testApi: MahoBoostTestApi = {
      flush,
      focus: () => scheduleViewAction(true),
      measure: () => scheduleViewAction(false),
      mounted: () => viewRef.current !== null,
      readCss: () => viewRef.current?.state.doc.toString() ?? null,
      writeCss: css => writeValue(css, true),
    };
    window.__mahoBoostTest = testApi;
    return () => {
      if (window.__mahoBoostTest === testApi) {
        delete window.__mahoBoostTest;
      }
    };
  }, [flush, scheduleViewAction, writeValue]);

  return {
    appendText: text => {
      const view = viewRef.current;
      if (!view || disabledRef.current) {
        return;
      }
      const from = view.state.doc.length;
      view.dispatch({changes: {from, insert: text}});
      view.focus();
    },
    focus: () => viewRef.current?.focus(),
    hostRef,
    mounted,
    requestMeasure: () => scheduleViewAction(false),
  };
}
