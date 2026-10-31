import '@testing-library/jest-dom';
import { options } from 'preact';

// Flush passive effects on a deterministic microtask outside act as well.
// Preact's default RAF/fallback timer can outlive jsdom teardown; execute the
// effects rather than dropping that work or waiting for an arbitrary frame.
options.requestAnimationFrame = queueMicrotask;
