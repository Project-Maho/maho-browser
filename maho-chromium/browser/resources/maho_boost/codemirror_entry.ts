// Copyright 2026 Maho Browser. All rights reserved.

import {EditorState} from '@codemirror/state';
import {EditorView, basicSetup} from 'codemirror';
import {css} from '@codemirror/lang-css';
import {oneDark} from '@codemirror/theme-one-dark';

declare global {
  interface Window {
    MahoCodeMirror: {
      EditorState: typeof EditorState;
      EditorView: typeof EditorView;
      basicSetup: typeof basicSetup;
      css: typeof css;
      oneDark: typeof oneDark;
    };
  }
}

window.MahoCodeMirror = {
  EditorState,
  EditorView,
  basicSetup,
  css,
  oneDark,
};
