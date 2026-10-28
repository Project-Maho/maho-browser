// Auto-generated from maho_cosmetic_runtime.js and maho_cosmetic_procedural.js.

constexpr char kMahoCosmeticRuntime[] = R"MAHO_JS(
// Copyright 2026 Maho Browser. All rights reserved.

(function() {
  'use strict';

  var api = window.__mahoCosmetic || (window.__mahoCosmetic = {});
  var pending = [];
  var known = new Set();
  var mutationCounter = 0;
  var generichide = false;
  var observer = null;
  var style = null;

  function addToken(token) {
    if (!token || known.has(token) || known.size >= 2048) return;
    known.add(token);
    if (pending.length < 2048) pending.push(token);
  }

  function collectElement(element) {
    if (!(element instanceof Element)) return;
    if (element.id) addToken('#' + element.id);
    for (var i = 0; i < element.classList.length; ++i) {
      addToken('.' + element.classList.item(i));
    }
  }

  function collectTree(node) {
    if (node.nodeType !== Node.ELEMENT_NODE) return;
    collectElement(node);
    var elements = node.querySelectorAll('*');
    for (var i = 0; i < elements.length; ++i) collectElement(elements[i]);
  }

  function addGenericSelectors(selectors) {
    if (!Array.isArray(selectors) || !selectors.length) return;
    if (!style || !style.isConnected) {
      style = document.createElement('style');
      style.id = 'maho-cosmetic-generic-style';
      (document.head || document.documentElement).appendChild(style);
    }
    style.appendChild(document.createTextNode(selectors.join(', ') +
        ' { display: none !important; }\n'));
  }

  api.init = function(payload) {
    payload = payload && typeof payload === 'object' ? payload : {};
    generichide = payload.generichide === true;
    api.exceptions = Array.isArray(payload.exceptions) ? payload.exceptions.slice() : [];
    api.generichide = generichide;
    api.proceduralActions = Array.isArray(payload.proceduralActions) ?
        payload.proceduralActions.slice() : [];

    if (Array.isArray(payload.hideSelectors) && payload.hideSelectors.length) {
      if (!style || !style.isConnected) {
        style = document.createElement('style');
        style.id = 'maho-cosmetic-style';
        (document.head || document.documentElement).appendChild(style);
      }
      style.appendChild(document.createTextNode(payload.hideSelectors.join(', ') +
          ' { display: none !important; }\n'));
    }

    if (!generichide) {
      collectTree(document.documentElement);
      if (!observer && document.documentElement) {
        observer = new MutationObserver(function(records) {
          mutationCounter++;
          for (var r = 0; r < records.length; ++r) {
            var record = records[r];
            if (record.type === 'attributes') collectElement(record.target);
            for (var n = 0; n < record.addedNodes.length; ++n) {
              collectTree(record.addedNodes[n]);
            }
          }
        });
        observer.observe(document.documentElement, {
          subtree: true, childList: true, attributes: true,
          attributeFilter: ['class', 'id'],
        });
      }
    }
    if (api.runProcedural) api.runProcedural(api.proceduralActions);
  };

  api.drainBatches = function() {
    var batch = pending;
    pending = [];
    var result = {batches: batch.length ? [batch] : [], mutations: mutationCounter};
    return JSON.stringify(result);
  };
  api.readMutationCounter = function() { return mutationCounter; };
  api.addGenericSelectors = addGenericSelectors;
  api.getMutationCounter = function() { return mutationCounter; };
})();
)MAHO_JS";

constexpr char kMahoCosmeticProcedural[] = R"MAHO_JS(
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
)MAHO_JS";
