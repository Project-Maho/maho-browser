// Copyright 2026 Maho Browser. All rights reserved.

import {Alert, AlertDescription} from '@ui/alert';
import {Toaster} from '@ui/sonner';
import {AlertCircle, CheckCircle, Info, Loader2} from '@icons/lucide';

export type StatusFeedbackState =
  | {readonly kind: 'idle'}
  | {readonly kind: 'loading'; readonly message: string}
  | {readonly kind: 'success'; readonly message: string}
  | {readonly kind: 'warning'; readonly message: string}
  | {readonly kind: 'error'; readonly message: string};

export interface StatusFeedbackProps {
  readonly state: StatusFeedbackState;
}

export function BoostToaster() {
  return (
    <Toaster
      className="boost-toaster"
      position="top-center"
      visibleToasts={1}
    />
  );
}

export function StatusFeedback({state}: StatusFeedbackProps) {
  switch (state.kind) {
    case 'idle':
      return null;
    case 'loading':
      return (
        <div
          aria-atomic="true"
          aria-live="polite"
          className="boost-status boost-status--loading"
          data-status="loading"
          role="status">
          <Loader2 aria-hidden="true" className="boost-control-icon boost-status__icon animate-spin" />
          <span>{state.message}</span>
        </div>
      );
    case 'success':
      return (
        <Alert
          aria-atomic="true"
          aria-live="polite"
          className="boost-status boost-status--alert"
          data-status="success"
          role="status"
          variant="success">
          <CheckCircle aria-hidden="true" className="boost-control-icon boost-status__icon" />
          <AlertDescription>{state.message}</AlertDescription>
        </Alert>
      );
    case 'warning':
      return (
        <Alert
          aria-atomic="true"
          aria-live="polite"
          className="boost-status boost-status--alert"
          data-status="warning"
          role="status"
          variant="warning">
          <Info aria-hidden="true" className="boost-control-icon boost-status__icon" />
          <AlertDescription>{state.message}</AlertDescription>
        </Alert>
      );
    case 'error':
      return (
        <Alert
          aria-atomic="true"
          aria-live="assertive"
          className="boost-status boost-status--alert"
          data-status="error"
          role="alert"
          variant="destructive">
          <AlertCircle aria-hidden="true" className="boost-control-icon boost-status__icon" />
          <AlertDescription>{state.message}</AlertDescription>
        </Alert>
      );
  }
}
