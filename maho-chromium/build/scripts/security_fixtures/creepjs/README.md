# CreepJS Offline Fixture

This fixture runs the real bundled CreepJS browser audit from revision
`10aa6724cd33a1015db1574211890518cd04f0cc` entirely on the fixture's loopback
origin.

## Authorship and license

- `LICENSE` is the byte-identical upstream CreepJS MIT license.
- `index.html` is the byte-identical upstream CreepJS audit document. `pages.js`
  uses its body markup so the audit has the DOM it expects.
- `creep.js` is upstream CreepJS's already-bundled `docs/creep.js`, with only the
  offline-boundary and completion-signal transformations recorded in
  `manifest.json`.
- `README.md` and `manifest.json` are Maho-authored provenance and integration
  metadata. The marker probes and result envelope in `../pages.js` are also
  Maho-authored; they are not represented as CreepJS findings.

The original upstream bytes were fetched from
`https://raw.githubusercontent.com/abrahamjuliot/creepjs/<revision>/<upstream_path>`.
Their SHA-256 values are recorded in `manifest.json`; the Git blob IDs were
recomputed as `SHA1("blob " + byte_length + NUL + bytes)` before import and
matched the pinned inventory.

## Offline transformations

The local `creep.js` differs from upstream only as follows:

1. Add `assertMahoLocalURL`, which resolves a URL and rejects it unless its
   origin equals the executing document or worker origin.
2. Pass the upstream bundle's script-size `fetch`, dedicated worker, shared
   worker, and service-worker registration URLs through that guard.
3. Replace the public Google STUN server list with `iceServers: []`, retaining
   host-only WebRTC fingerprinting without outbound STUN traffic.
4. Dispatch `maho-creepjs-complete` after upstream exposes `window.Fingerprint`
   and `window.Creep`, allowing the Maho wrapper to post the genuine audit.

Tests enumerate every active network constructor in the bundle, require each URL
sink to use the same-origin guard, execute that guard against local and external
URLs, and verify the STUN configuration is empty. The only application report
is the Maho-owned same-origin POST to `/creepjs/results`.
