// Copyright 2026 Maho Browser. All rights reserved.
declare module '*/maho_mail.mojom-webui.js' {
  export type MailHandlerResult = import('../maho_mail.mojom-webui.js.d').MailHandlerResult;
  type MojomPageHandlerRemote = import('../maho_mail.mojom-webui.js.d').PageHandlerRemote;
  export type PageHandlerRemote = Omit<MojomPageHandlerRemote, 'beginOAuth'> & {
    beginOAuth(provider: string, reauthorizeAccountId: string): Promise<{ ok: boolean; errorJson: string; state: string }>;
  };
  export const PageHandlerRemote: {
    new (): PageHandlerRemote;
  };
  export type PageCallbackRouter = import('../maho_mail.mojom-webui.js.d').PageCallbackRouter;
  export const PageCallbackRouter: typeof import('../maho_mail.mojom-webui.js.d').PageCallbackRouter;
  export type PageHandlerFactory = import('../maho_mail.mojom-webui.js.d').PageHandlerFactory;
  export const PageHandlerFactory: typeof import('../maho_mail.mojom-webui.js.d').PageHandlerFactory;
}

declare module '*?raw' {
  const content: string;
  export default content;
}
