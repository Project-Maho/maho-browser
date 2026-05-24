// Auto-generated from maho_dom_screenshot_script.js — do not edit directly.

constexpr char kDomScreenshotScript[] = R"MAHO_JS(
(function() {
  'use strict';

  if (window.__mahoDomScreenshotActive) return;
  window.__mahoDomScreenshotActive = true;

  var sessionToken = window.__mahoDomScreenshotToken;

  var overlay = null;
  var highlightEl = null;
  var instructionEl = null;
  var hoveredElement = null;
  var isDragging = false;
  var dragStartX = 0;
  var dragStartY = 0;
  var dragRectEl = null;

  function clamp(val, min, max) {
    return Math.max(min, Math.min(max, val));
  }

  function getBestRectForElement(el) {
    var rect = el.getBoundingClientRect();
    var MIN_AREA = 400;
    while (el && rect.width * rect.height < MIN_AREA && el.parentElement) {
      el = el.parentElement;
      rect = el.getBoundingClientRect();
    }
    var vw = window.innerWidth;
    var vh = window.innerHeight;
    return {
      x: clamp(rect.left, 0, vw),
      y: clamp(rect.top, 0, vh),
      width: clamp(rect.width, 1, vw - clamp(rect.left, 0, vw)),
      height: clamp(rect.height, 1, vh - clamp(rect.top, 0, vh)),
    };
  }

  function positionHighlight(rect) {
    if (!highlightEl) return;
    highlightEl.style.left = rect.x + 'px';
    highlightEl.style.top = rect.y + 'px';
    highlightEl.style.width = rect.width + 'px';
    highlightEl.style.height = rect.height + 'px';
    highlightEl.style.display = 'block';
  }

  function hideHighlight() {
    if (highlightEl) highlightEl.style.display = 'none';
  }

  function createOverlay() {
    overlay = document.createElement('div');
    overlay.id = '__maho-dom-screenshot-overlay';
    overlay.style.cssText = [
      'position:fixed', 'top:0', 'left:0',
      'width:100vw', 'height:100vh',
      'z-index:2147483647',
      'cursor:crosshair',
      'background:rgba(0,0,0,0.25)',
      'user-select:none',
      '-webkit-user-select:none',
      'box-sizing:border-box',
    ].join(';');

    highlightEl = document.createElement('div');
    highlightEl.style.cssText = [
      'position:fixed', 'display:none',
      'border:2px solid rgba(100,160,255,0.9)',
      'background:rgba(100,160,255,0.12)',
      'border-radius:2px',
      'pointer-events:none',
      'z-index:2147483647',
      'box-sizing:border-box',
      'transition:left 0.05s,top 0.05s,width 0.05s,height 0.05s',
    ].join(';');

    dragRectEl = document.createElement('div');
    dragRectEl.style.cssText = [
      'position:fixed', 'display:none',
      'border:2px dashed rgba(100,200,100,0.9)',
      'background:rgba(100,200,100,0.08)',
      'border-radius:2px',
      'pointer-events:none',
      'z-index:2147483647',
      'box-sizing:border-box',
    ].join(';');

    instructionEl = document.createElement('div');
    instructionEl.textContent = 'Click an element or drag to select a region. Press Esc to cancel.';
    instructionEl.style.cssText = [
      'position:fixed', 'top:12px', 'left:50%',
      'transform:translateX(-50%)',
      'background:rgba(0,0,0,0.75)',
      'color:#fff',
      'font-family:-apple-system,BlinkMacSystemFont,sans-serif',
      'font-size:13px',
      'padding:6px 16px',
      'border-radius:20px',
      'pointer-events:none',
      'z-index:2147483647',
      'white-space:nowrap',
      'letter-spacing:0.01em',
    ].join(';');

    document.documentElement.appendChild(overlay);
    document.documentElement.appendChild(highlightEl);
    document.documentElement.appendChild(dragRectEl);
    document.documentElement.appendChild(instructionEl);
  }

  function cleanup() {
    if (overlay) { overlay.remove(); overlay = null; }
    if (highlightEl) { highlightEl.remove(); highlightEl = null; }
    if (dragRectEl) { dragRectEl.remove(); dragRectEl = null; }
    if (instructionEl) { instructionEl.remove(); instructionEl = null; }
    document.removeEventListener('keydown', onKeyDown, true);
    window.__mahoDomScreenshotActive = false;
  }

  function reportRect(rect) {
    cleanup();
    window.__mahoDomScreenshotResultData = {
      session: sessionToken,
      x: rect.x,
      y: rect.y,
      width: rect.width,
      height: rect.height,
    };
  }

  function onMouseMove(e) {
    if (isDragging) {
      updateDragRect(e);
      return;
    }

    overlay.style.display = 'none';
    highlightEl.style.display = 'none';

    var el = document.elementFromPoint(e.clientX, e.clientY);

    overlay.style.display = '';
    highlightEl.style.display = '';

    if (!el || el === overlay || el === highlightEl ||
        el === dragRectEl || el === instructionEl) {
      hideHighlight();
      hoveredElement = null;
      return;
    }

    if (el === document.body || el === document.documentElement) {
      hideHighlight();
      hoveredElement = null;
      return;
    }

    hoveredElement = el;
    var rect = getBestRectForElement(el);
    positionHighlight(rect);
  }

  function onMouseDown(e) {
    if (e.button !== 0) return;
    e.preventDefault();
    e.stopPropagation();
    isDragging = false;
    dragStartX = e.clientX;
    dragStartY = e.clientY;

    overlay.addEventListener('mousemove', onDragMove, true);
    overlay.addEventListener('mouseup', onMouseUp, true);
  }

  function onDragMove(e) {
    var dx = Math.abs(e.clientX - dragStartX);
    var dy = Math.abs(e.clientY - dragStartY);
    if (dx > 5 || dy > 5) {
      if (!isDragging) {
        isDragging = true;
        hideHighlight();
        if (instructionEl) {
          instructionEl.textContent = 'Release to capture selection.';
        }
      }
      updateDragRect(e);
    }
  }

  function updateDragRect(e) {
    var x = Math.min(e.clientX, dragStartX);
    var y = Math.min(e.clientY, dragStartY);
    var w = Math.abs(e.clientX - dragStartX);
    var h = Math.abs(e.clientY - dragStartY);
    if (dragRectEl) {
      dragRectEl.style.display = 'block';
      dragRectEl.style.left = x + 'px';
      dragRectEl.style.top = y + 'px';
      dragRectEl.style.width = w + 'px';
      dragRectEl.style.height = h + 'px';
    }
  }

  function onMouseUp(e) {
    overlay.removeEventListener('mousemove', onDragMove, true);
    overlay.removeEventListener('mouseup', onMouseUp, true);

    if (isDragging) {
      var x = Math.min(e.clientX, dragStartX);
      var y = Math.min(e.clientY, dragStartY);
      var w = Math.abs(e.clientX - dragStartX);
      var h = Math.abs(e.clientY - dragStartY);
      isDragging = false;
      if (w < 4 || h < 4) {
        finishWithHoveredElement();
        return;
      }
      reportRect({ x: x, y: y, width: w, height: h });
      return;
    }

    finishWithHoveredElement();
  }

  function finishWithHoveredElement() {
    if (hoveredElement) {
      var rect = getBestRectForElement(hoveredElement);
      if (rect.width > 0 && rect.height > 0) {
        reportRect(rect);
        return;
      }
    }
    reportRect({
      x: 0, y: 0,
      width: window.innerWidth,
      height: window.innerHeight,
    });
  }

  function onKeyDown(e) {
    if (e.key === 'Escape') {
      e.preventDefault();
      e.stopPropagation();
      cleanup();
      window.__mahoDomScreenshotResultData = { session: sessionToken, cancelled: true };
    }
  }

  createOverlay();

  overlay.addEventListener('mousemove', onMouseMove, true);
  overlay.addEventListener('mousedown', onMouseDown, true);
  document.addEventListener('keydown', onKeyDown, true);

})();
)MAHO_JS";
