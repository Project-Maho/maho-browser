// Copyright 2026 Maho Browser. All rights reserved.
//
// Content script for the owned incognito test extension fixture. It injects a
// single deterministic marker node so the Maho incognito privacy browser test
// can assert content-script presence (regular / incognito-allowed) or absence
// (incognito default-off) by DOM lookup alone, without ever reading a Maho
// extension snapshot.

(function () {
  const marker = document.createElement('div');
  marker.id = 'maho-incognito-test-extension-marker';
  marker.setAttribute('data-maho-marker', 'MAHO_INCOGNITO_TEST_EXTENSION_ACTIVE');
  marker.style.display = 'none';
  document.documentElement.appendChild(marker);
})();
