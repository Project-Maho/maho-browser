// Copyright 2026 Maho Browser. All rights reserved.

import * as React from 'react';

interface ToastProps {
  text: string;
  tone?: string;
  onDismiss?: () => void;
}

// TODO(maho): Restore real toast UI; this is a build-time stub used while the
// real implementation is being reintegrated.
export function Toast(_: ToastProps) {
  return null;
}
