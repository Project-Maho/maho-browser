export {};

declare module '../../maho_settings.mojom-webui.js' {
  interface InstalledExtension {
    manifestVersion: number;
  }

  interface PageHandlerRemote {
    getBillingPortalUrl(): Promise<{url: string}>;
    getBuyCreditsUrl(packSizeUsd: 10 | 50 | 100): Promise<{checkoutUrl: string}>;
    getInvoices(): Promise<{invoices: Invoice[]}>;
    openExtensionsPage(): Promise<void>;
  }
}
