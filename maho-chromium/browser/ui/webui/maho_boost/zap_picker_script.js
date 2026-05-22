// Copyright 2026 Maho Browser. All rights reserved.

(function() {
  'use strict';

  var _boostId = '';
  var _hovered = null;
  var _overlay = null;

  function getSelector(el) {
    if (!el || el === document.body || el === document.documentElement) {
      return null;
    }
    if (el.id) {
      return '#' + CSS.escape(el.id);
    }
    var parts = [];
    var node = el;
    while (node && node !== document.documentElement) {
      var tag = node.tagName.toLowerCase();
      var idx = 1;
      var sib = node.previousElementSibling;
      while (sib) {
        if (sib.tagName === node.tagName) idx++;
        sib = sib.previousElementSibling;
      }
      parts.unshift(tag + ':nth-of-type(' + idx + ')');
      node = node.parentElement;
    }
    return parts.join(' > ');
  }

  function positionOverlay(el) {
    if (!_overlay) return;
    var r = el.getBoundingClientRect();
    _overlay.style.top = (r.top + window.scrollY) + 'px';
    _overlay.style.left = (r.left + window.scrollX) + 'px';
    _overlay.style.width = r.width + 'px';
    _overlay.style.height = r.height + 'px';
    _overlay.style.display = 'block';
  }

  function onMouseOver(e) {
    var el = e.target;
    if (el === _overlay) return;
    _hovered = el;
    positionOverlay(el);
  }

  function onMouseOut() {
    if (_overlay) _overlay.style.display = 'none';
    _hovered = null;
  }

  function onClick(e) {
    e.preventDefault();
    e.stopPropagation();
    var sel = _hovered ? getSelector(_hovered) : null;
    if (sel) {
      window.__mahoZapPickerResult = JSON.stringify({ boostId: _boostId, selector: sel });
      document.dispatchEvent(new CustomEvent('__maho_zap_picked', {
        detail: JSON.stringify({ boostId: _boostId, selector: sel })
      }));
    }
    cleanup();
  }

  function onKeyDown(e) {
    if (e.key === 'Escape') {
      window.__mahoZapPickerResult = JSON.stringify({ boostId: _boostId, selector: '' });
      cleanup();
    }
  }

  function cleanup() {
    document.removeEventListener('mouseover', onMouseOver, true);
    document.removeEventListener('mouseout', onMouseOut, true);
    document.removeEventListener('click', onClick, true);
    document.removeEventListener('keydown', onKeyDown, true);
    if (_overlay && _overlay.parentNode) {
      _overlay.parentNode.removeChild(_overlay);
    }
    _overlay = null;
    window.__mahoZapPickerActive = false;
  }

  window.__mahoStartZapPicker = function(boostId) {
    if (window.__mahoZapPickerActive) {
      cleanup();
    }

    window.__mahoZapPickerActive = true;
    window.__mahoZapPickerResult = '';
    _boostId = boostId;

    _overlay = document.createElement('div');
    _overlay.style.cssText = [
      'position:absolute', 'pointer-events:none', 'z-index:2147483646',
      'outline:2px solid #7c3aed', 'background:rgba(124,58,237,0.08)',
      'display:none', 'box-sizing:border-box',
    ].join(';');
    document.documentElement.appendChild(_overlay);

    document.addEventListener('mouseover', onMouseOver, true);
    document.addEventListener('mouseout', onMouseOut, true);
    document.addEventListener('click', onClick, true);
    document.addEventListener('keydown', onKeyDown, true);
  };
})();
