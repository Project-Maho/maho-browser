// Copyright 2026 Maho Browser. All rights reserved.

(function() {
  'use strict';
  var api = window.__mahoCosmetic;
  if (!api) return;

  var pendingActions = [];
  var scheduled = false;
  var lastRun = 0;

  function scheduleProceduralRun() {
    if (scheduled || !pendingActions.length) return;
    scheduled = true;
    var delay = Math.max(0, 50 - (performance.now() - lastRun));
    setTimeout(function() {
      scheduled = false;
      var actions = pendingActions;
      pendingActions = [];
      lastRun = performance.now();
      api.runProcedural(actions);
      scheduleProceduralRun();
    }, delay);
  }

  api.runProcedural = function(actions, options) {
    options = options || {};
    var started = performance.now();
    var maxActions = Math.min(options.maxActions || 4096, 4096);
    var timeBudget = Math.min(options.timeBudgetMs || 50, 50);
    var applied = 0;
    var unsupported = 0;
    var stopped = false;

    function decode(raw) {
      if (typeof raw === 'string') {
        try { return JSON.parse(raw); } catch (_) { return null; }
      }
      return raw;
    }
    function parseArg(text) {
      var colon = text.indexOf(':');
      if (colon < 0) return null;
      return [text.slice(0, colon).trim(), text.slice(colon + 1).trim()];
    }
    function matches(element, op) {
      if (op.type === 'css-selector') return element.matches(op.arg);
      var pair;
      if (op.type === 'has-text') return element.textContent.includes(op.arg);
      if (op.type === 'matches-css' || op.type === 'matches-css-before' ||
          op.type === 'matches-css-after') {
        var css = op.arg;
        pair = parseArg(css);
        var pseudo = op.type === 'matches-css-before' ? '::before' :
            op.type === 'matches-css-after' ? '::after' : null;
        return !!pair && new RegExp(pair[1]).test(
            window.getComputedStyle(element, pseudo).getPropertyValue(pair[0]));
      }
      if (op.type === 'matches-attr') {
        pair = parseArg(op.arg);
        return !!pair && new RegExp(pair[1]).test(element.getAttribute(pair[0]) || '');
      }
      if (op.type === 'matches-path') return new RegExp(op.arg).test(location.pathname);
      if (op.type === 'min-text-length') return element.textContent.trim().length >= Number(op.arg);
      if (op.type === 'upward') {
        var target = op.arg;
        if (/^\d+$/.test(target)) {
          var parent = element;
          for (var i = 0, n = Math.min(Number(target), 100); i < n && parent; ++i) parent = parent.parentElement;
          return !!parent;
        }
        return !!element.closest(target);
      }
      if (op.type === 'xpath') {
        try {
          return document.evaluate(op.arg, element, null, XPathResult.FIRST_ORDERED_NODE_TYPE, null)
              .singleNodeValue === element;
        } catch (_) { return false; }
      }
      return null;
    }
    function applyAction(element, action) {
      if (action !== undefined && action !== null &&
          ['remove', 'style', 'remove-attr', 'remove-class'].indexOf(action.type) < 0) {
        return false;
      }
      if (action === undefined || action === null || action.type === 'remove') {
        element.remove();
      } else if (action.type === 'style') {
        var declarations = String(action.arg).split(';');
        for (var i = 0; i < declarations.length; ++i) {
          var colon = declarations[i].indexOf(':');
          if (colon > 0) element.style.setProperty(declarations[i].slice(0, colon).trim(),
              declarations[i].slice(colon + 1).replace(/\s*!important\s*$/i, '').trim(), 'important');
        }
      } else if (action.type === 'remove-attr') {
        element.removeAttribute(action.arg);
      } else if (action.type === 'remove-class') {
        element.classList.remove(action.arg);
      } else {
        return false;
      }
      return true;
    }

    if (!Array.isArray(actions)) return {applied: 0, unsupported: 0, stopped: false};
    var processed = 0;
    for (var a = 0; a < actions.length; ++a) {
      if (processed >= maxActions || performance.now() - started >= timeBudget) { stopped = true; break; }
      var rule = decode(actions[a]);
      if (!rule || !Array.isArray(rule.selector) || !rule.selector.length) { unsupported++; continue; }
      var candidates;
      var first = rule.selector[0];
      try {
        if (!first || first.type !== 'css-selector' || typeof first.arg !== 'string') { unsupported++; continue; }
        candidates = Array.from(document.querySelectorAll(first.arg)).slice(0, 2048);
      } catch (_) { unsupported++; continue; }
      for (var e = 0; e < candidates.length; ++e) {
        if (performance.now() - started >= timeBudget) { stopped = true; break; }
        if (processed >= maxActions) { stopped = true; break; }
        processed++;
        var element = candidates[e];
        var matched = true;
        for (var s = 1; s < rule.selector.length; ++s) {
          var result;
          try { result = matches(element, rule.selector[s]); } catch (_) { result = null; }
          if (result === null) { unsupported++; matched = false; break; }
          if (!result) { matched = false; break; }
        }
        if (matched && applyAction(element, rule.action)) applied++;
        else if (matched) unsupported++;
      }
      if (stopped) break;
    }
    if (stopped && a < actions.length) {
      Array.prototype.push.apply(pendingActions, actions.slice(a));
      scheduleProceduralRun();
    }
    return {applied: applied, unsupported: unsupported, stopped: stopped};
  };

  api.scheduleProceduralRun = scheduleProceduralRun;
})();
