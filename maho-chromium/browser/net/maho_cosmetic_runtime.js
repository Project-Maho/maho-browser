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
