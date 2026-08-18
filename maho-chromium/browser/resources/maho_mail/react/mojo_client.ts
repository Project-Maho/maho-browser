// Copyright 2026 Maho Browser. All rights reserved.

import { PageHandlerRemote, PageCallbackRouter, PageHandlerFactory } from '../maho_mail.mojom-webui.js';

export const handler = new PageHandlerRemote();
export const callbackRouter = new PageCallbackRouter();

const factory = PageHandlerFactory.getRemote();
factory.createPageHandler(
  callbackRouter.$.bindNewPipeAndPassRemote(),
  handler.$.bindNewPipeAndPassReceiver()
);

export function closeMojoClient(): void {
  (handler.$ as {close?: () => void}).close?.();
  (callbackRouter.$ as {close?: () => void}).close?.();
}
