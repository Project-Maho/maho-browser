// Auto-generated from inline_edit_script.js — do not edit directly.

constexpr char kInlineEditScript[] = R"MAHO_JS(
// Copyright 2026 Maho Browser. All rights reserved.

(function() {
  'use strict';

  if (window.__mahoInlineEditInjected) return;
  window.__mahoInlineEditInjected = true;

  let currentSelection = null;
  let originalText = null;
  let toolbarEl = null;
  let pendingResult = null;

  const ACTIONS = [
    { id: 'rewrite',     label: '\u2728 Rewrite',     instruction: 'Rewrite the following text, improving clarity and flow. Return ONLY the rewritten text.\n\nOriginal: ' },
    { id: 'fix-grammar', label: '\uD83D\uDCDD Fix Grammar', instruction: 'Fix grammar and spelling in the following text. Return ONLY the corrected text.\n\nOriginal: ' },
    { id: 'shorter',     label: '\uD83D\uDCCF Shorter',     instruction: 'Make this text more concise while keeping the meaning. Return ONLY the shortened text.\n\nOriginal: ' },
    { id: 'longer',      label: '\uD83D\uDCD0 Longer',      instruction: 'Expand this text with more detail and context. Return ONLY the expanded text.\n\nOriginal: ' },
  ];

  function createToolbar() {
    if (toolbarEl) return toolbarEl;

    var el = document.createElement('div');
    el.id = '__maho-inline-edit-toolbar';
    el.style.cssText = [
      'position:fixed', 'z-index:2147483647', 'display:none',
      'background:#1e1e2e', 'border:1px solid #333', 'border-radius:8px',
      'box-shadow:0 4px 12px rgba(0,0,0,0.4)', 'padding:4px',
      'font-family:-apple-system,BlinkMacSystemFont,sans-serif',
      'font-size:12px', 'color:#cdd6f4', 'gap:2px',
      'flex-direction:row', 'align-items:center',
    ].join(';');

    for (var i = 0; i < ACTIONS.length; i++) {
      (function(action) {
        var btn = document.createElement('button');
        btn.textContent = action.label;
        btn.dataset.action = action.id;
        btn.style.cssText = [
          'background:transparent', 'border:none', 'color:#89b4fa',
          'cursor:pointer', 'padding:4px 8px', 'border-radius:4px',
          'font-size:11px', 'font-weight:500', 'white-space:nowrap',
        ].join(';');
        btn.addEventListener('mouseenter', function() { btn.style.background = '#313244'; });
        btn.addEventListener('mouseleave', function() { btn.style.background = 'transparent'; });
        btn.addEventListener('mousedown', function(e) {
          e.preventDefault();
          e.stopPropagation();
          handleAction(action);
        });
        el.appendChild(btn);
      })(ACTIONS[i]);
    }

    var resultArea = document.createElement('div');
    resultArea.id = '__maho-ie-result';
    resultArea.style.cssText = 'display:none;padding:6px 8px;max-width:400px;';

    var resultText = document.createElement('div');
    resultText.id = '__maho-ie-result-text';
    resultText.style.cssText = 'margin-bottom:6px;line-height:1.4;max-height:120px;overflow-y:auto;';

    var resultButtons = document.createElement('div');
    resultButtons.style.cssText = 'display:flex;gap:6px;';

    var acceptBtn = document.createElement('button');
    acceptBtn.textContent = 'Accept';
    acceptBtn.id = '__maho-ie-accept';
    acceptBtn.style.cssText = 'background:transparent;border:none;color:#a6e3a1;cursor:pointer;font-size:11px;font-weight:600;padding:2px 6px;';
    acceptBtn.addEventListener('mousedown', function(e) {
      e.preventDefault();
      applyResult();
    });

    var rejectBtn = document.createElement('button');
    rejectBtn.textContent = 'Reject';
    rejectBtn.style.cssText = 'background:transparent;border:none;color:#a6adc8;cursor:pointer;font-size:11px;font-weight:500;padding:2px 6px;';
    rejectBtn.addEventListener('mousedown', function(e) {
      e.preventDefault();
      hideToolbar();
    });

    resultButtons.appendChild(acceptBtn);
    resultButtons.appendChild(rejectBtn);
    resultArea.appendChild(resultText);
    resultArea.appendChild(resultButtons);
    el.appendChild(resultArea);

    document.documentElement.appendChild(el);
    toolbarEl = el;
    return el;
  }

  function showToolbar(x, y) {
    var tb = createToolbar();
    tb.style.display = 'flex';
    var buttons = tb.querySelectorAll('button[data-action]');
    for (var i = 0; i < buttons.length; i++) {
      buttons[i].style.display = '';
    }
    var result = tb.querySelector('#__maho-ie-result');
    if (result) result.style.display = 'none';

    tb.style.left = Math.max(4, x) + 'px';
    tb.style.top = Math.max(4, y - 40) + 'px';
  }

  function hideToolbar() {
    if (toolbarEl) {
      toolbarEl.style.display = 'none';
    }
    currentSelection = null;
  }

  var selectionTimeout = null;

  document.addEventListener('selectionchange', function() {
    clearTimeout(selectionTimeout);
    selectionTimeout = setTimeout(onSelectionChange, 200);
  });

  function onSelectionChange() {
    var sel = window.getSelection();
    if (!sel || sel.isCollapsed || sel.rangeCount === 0) {
      hideToolbar();
      return;
    }

    var range = sel.getRangeAt(0);
    var container = range.commonAncestorContainer;
    var element = container.nodeType === 1 ? container : container.parentElement;
    if (!element) { hideToolbar(); return; }

    var editableParent = element.closest('[contenteditable="true"], input, textarea');
    if (!editableParent) { hideToolbar(); return; }

    var text = sel.toString();
    if (text.trim().length < 2) { hideToolbar(); return; }

    var rect = range.getBoundingClientRect();
    currentSelection = {
      range: range.cloneRange(),
      element: editableParent,
      text: text,
      isInput: editableParent.tagName === 'INPUT' || editableParent.tagName === 'TEXTAREA',
    };

    showToolbar(rect.x + rect.width / 2 - 150, rect.y);
  }

  function handleAction(action) {
    if (!currentSelection) return;

    var instruction = action.instruction + currentSelection.text;
    showResultArea('Thinking\u2026');

    // Bridge to C++ handler — dispatches event with serialized request
    document.dispatchEvent(new CustomEvent('__maho_inline_edit_request', {
      detail: JSON.stringify({
        type: 'edit_request',
        text: currentSelection.text,
        instruction: instruction,
      })
    }));
  }

  function showResultArea(text) {
    var tb = createToolbar();
    var buttons = tb.querySelectorAll('button[data-action]');
    for (var i = 0; i < buttons.length; i++) {
      buttons[i].style.display = 'none';
    }
    var result = tb.querySelector('#__maho-ie-result');
    var resultText = tb.querySelector('#__maho-ie-result-text');
    if (result) result.style.display = 'block';
    if (resultText) resultText.textContent = text;
  }

  // Called from C++ via ExecuteJavaScript when LLM result arrives
  window.__mahoInlineEditResult = function(resultText) {
    pendingResult = resultText;
    showResultArea(resultText);
  };

  // Called from C++ via ExecuteJavaScript on error
  window.__mahoInlineEditError = function(errorMsg) {
    showResultArea('Error: ' + errorMsg);
  };

  function applyResult() {
    if (!currentSelection || !pendingResult) return;

    var sel = currentSelection;
    originalText = sel.text;

    if (sel.isInput) {
      var start = sel.element.selectionStart;
      var end = sel.element.selectionEnd;
      sel.element.value = sel.element.value.substring(0, start) + pendingResult + sel.element.value.substring(end);
      sel.element.selectionStart = start;
      sel.element.selectionEnd = start + pendingResult.length;
      sel.element.dispatchEvent(new Event('input', { bubbles: true }));
    } else {
      var winSel = window.getSelection();
      winSel.removeAllRanges();
      winSel.addRange(sel.range);
      document.execCommand('insertText', false, pendingResult);
    }

    hideToolbar();
    pendingResult = null;
  }

  // Called from C++ to directly replace selected text
  window.__mahoReplaceSelection = function(newText) {
    if (!currentSelection) return false;
    pendingResult = newText;
    applyResult();
    return true;
  };

  window.__mahoGetSelectedText = function() {
    var sel = window.getSelection();
    return (sel && !sel.isCollapsed) ? sel.toString() : '';
  };

})();
)MAHO_JS";
