import { defineConfig } from 'vite';
import preact from '@preact/preset-vite';
import { visualizer } from 'rollup-plugin-visualizer';

export default defineConfig({
  plugins: [
    preact(),
    {
      name: 'maho-file-protocol',
      transformIndexHtml(html) {
        return html.replace(/\s+crossorigin(=("[^"]*"|'[^']*'|[^\s>]+))?/g, '');
      },
    },
    ...(process.env.ANALYZE === 'true'
      ? [
          visualizer({
            filename: 'dist/stats.html',
            open: false,
            gzipSize: true,
            brotliSize: true,
          }),
        ]
      : []),
  ],
  // Relative asset base + ES module code splitting output so the bundle loads under
  // file:// (WKWebView / Android WebView) while dynamically loading route chunks on demand.
  base: './',
  build: {
    outDir: 'dist',
    rollupOptions: {
      input: 'index.html',
      output: {
        format: 'es',
        entryFileNames: 'ai-bundle.js',
        chunkFileNames: 'chunks/[name]-[hash].js',
        assetFileNames: 'assets/[name]-[hash][extname]',
      },
    },
  },
  test: {
    environment: 'jsdom',
    globals: true,
    setupFiles: ['./tests/setup.ts'],
  },
});
