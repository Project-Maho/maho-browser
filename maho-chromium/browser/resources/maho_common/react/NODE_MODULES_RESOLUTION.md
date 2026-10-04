# Node Modules Resolution Strategy — WebUI React Bundles

## Current Resolution Path

All WebUI React surfaces (`maho_ai`, `maho_boost`, `maho_routines`, etc.) resolve
npm packages from the **workspace-level** `website/node_modules/` directory. This is
a deliberate coupling during early migration: the Astro website workspace already
has React, ReactDOM, and Radix UI installed, so WebUI bundles reuse those
installations rather than maintaining a separate `node_modules` tree.

### How it works

1. Each surface's `bundle_react.mjs` runs esbuild with the working directory set
   such that Node resolution walks up to `website/node_modules/`.
2. Each surface's `tsconfig.json` uses explicit `paths` mappings pointing to
   `../../../../../website/node_modules/@types/*` for type checking.
3. The `bundle_guard.mjs` esbuild plugin (in `maho_common/react/`) validates at
   bundle time that **all** resolved `node_modules` paths fall under the expected
   `website/node_modules/` root and rejects website-only packages (Astro, Vite,
   Starlight).

### Why this is acceptable for Wave 1

- WebUI surfaces only depend on React, ReactDOM, and Radix UI — packages that
  are also website dependencies and unlikely to diverge in version.
- The bundle guard catches accidental pulls of website-only packages.
- No runtime coupling exists: bundles are fully self-contained after esbuild.

### Risks and future mitigation (Wave 2+)

| Risk | Mitigation |
|------|-----------|
| Website upgrades React to an incompatible version | Pin WebUI-consumed packages in a dedicated `browser/resources/package.json` |
| New website dep conflicts with WebUI needs | The bundle guard will catch unexpected resolutions |
| CI environment lacks `website/node_modules/` | Build scripts must run `bun install` in `website/` before bundling |

### Bundle Guard Details

The `nodeModulesGuardPlugin` in `maho_common/react/bundle_guard.mjs`:

- Runs as an esbuild `onEnd` hook with `metafile` enabled
- Asserts every `node_modules` input resolves under the configured base path
- Rejects any resolution containing website-only package prefixes (`@astrojs/`,
  `astro`, `starlight`, `vite`)
- Throws a build-time error with the offending path if violated

### Adding the guard to a new surface

```js
import {nodeModulesGuardPlugin} from '../../maho_common/react/bundle_guard.mjs';

await esbuild.build({
  // ...
  plugins: [nodeModulesGuardPlugin('/path/to/website/node_modules')],
  metafile: true,
});
```
