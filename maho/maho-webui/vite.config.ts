import {defineConfig} from 'vite';
import {resolve} from 'path';

export default defineConfig({
  root: '.',
  build: {
    rollupOptions: {
      input: {
        main: resolve(__dirname, 'index.html'),
        sidebar: resolve(__dirname, 'sidebar/index.html'),
        ai: resolve(__dirname, 'ai/index.html'),
        settings: resolve(__dirname, 'settings/index.html'),
        notes: resolve(__dirname, 'notes/index.html'),
        boost: resolve(__dirname, 'boost/index.html'),
        easel: resolve(__dirname, 'easel/index.html'),
        spaces: resolve(__dirname, 'spaces/index.html'),
        test: resolve(__dirname, 'test/index.html'),
      },
    },
  },
});
