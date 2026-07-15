// Copyright 2026 Maho Browser. All rights reserved.

/**
 * C1 Shared Mojo hook — reusable pattern for binding a Mojom PageHandler.
 *
 * Each WebUI surface follows the same Mojo binding ceremony:
 *   1. Instantiate PageCallbackRouter
 *   2. Instantiate PageHandlerRemote
 *   3. Call PageHandlerFactory.getRemote().createPageHandler(router, handler)
 *
 * This hook encapsulates that pattern so surfaces don't duplicate the wiring.
 */

/**
 * Generic interface for a Mojo PageHandlerFactory that follows the standard
 * createPageHandler(callbackRemote, handlerReceiver) pattern.
 */
export interface MojoPageHandlerFactory<TRouter, THandler> {
  getRemote(): {
    createPageHandler(
      callbackRemote: unknown,
      handlerReceiver: unknown,
    ): void;
  };
}

/**
 * Bind a Mojo page handler following the standard WebUI pattern.
 *
 * Returns the instantiated router and handler. Callers provide their
 * surface-specific generated types.
 *
 * @example
 * ```ts
 * import {PageCallbackRouter, PageHandlerFactory, PageHandlerRemote}
 *     from '../maho_routines.mojom-webui.js';
 * import {bindMojoPageHandler} from '//resources/maho_common/react/use_mojo.js';
 *
 * const {router, handler} = bindMojoPageHandler(
 *     PageCallbackRouter, PageHandlerRemote, PageHandlerFactory);
 * ```
 */
export function bindMojoPageHandler<
  TRouter extends {$: {bindNewPipeAndPassRemote(): unknown}},
  THandler extends {$: {bindNewPipeAndPassReceiver(): unknown}},
>(
  RouterClass: new () => TRouter,
  HandlerClass: new () => THandler,
  Factory: {getRemote(): {createPageHandler(r: unknown, h: unknown): void}},
): {router: TRouter; handler: THandler} {
  const router = new RouterClass();
  const handler = new HandlerClass();
  Factory.getRemote().createPageHandler(
      router.$.bindNewPipeAndPassRemote(),
      handler.$.bindNewPipeAndPassReceiver());
  return {router, handler};
}
