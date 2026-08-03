// Copyright 2026 Maho Browser. All rights reserved.

import {
  PageCallbackRouter,
  PageHandlerFactory,
  PageHandlerRemote,
} from './mojo-runtime.js';
import type {PageObserver} from '../maho_boost.mojom-webui.js';

export class BoostBridge {
  readonly callbackRouter: PageCallbackRouter;
  readonly pageHandler: PageHandlerRemote;

  private constructor(callbackRouter: PageCallbackRouter, pageHandler: PageHandlerRemote) {
    this.callbackRouter = callbackRouter;
    this.pageHandler = pageHandler;
  }

  static connect(): BoostBridge {
    const callbackRouter = new PageCallbackRouter();
    const pageHandler = new PageHandlerRemote();
    PageHandlerFactory.getRemote().createPageHandler(
      callbackRouter.$.bindNewPipeAndPassRemote(),
      pageHandler.$.bindNewPipeAndPassReceiver(),
    );
    return new BoostBridge(callbackRouter, pageHandler);
  }

  addObserverListeners(observer: PageObserver): void {
    this.callbackRouter.onBoostsChanged.addListener(() => observer.onBoostsChanged());
    this.callbackRouter.onActiveChanged.addListener((boostId) => observer.onActiveChanged(boostId));
    this.callbackRouter.onZapStateUpdate.addListener((isOn, anyZapped) => {
      observer.onZapStateUpdate(isOn, anyZapped);
    });
    this.callbackRouter.onZapListUpdate.addListener(() => observer.onZapListUpdate());
    this.callbackRouter.onPickerStateUpdate.addListener((isOn) => observer.onPickerStateUpdate(isOn));
    this.callbackRouter.onPickerSelectorPicked.addListener((selector) => {
      observer.onPickerSelectorPicked(selector);
    });
    this.callbackRouter.onEditorKilled.addListener(() => observer.onEditorKilled());
  }
}
