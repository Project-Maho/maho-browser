// Copyright 2026 Maho Browser. All rights reserved.

interface ImportMetaEnv {
  readonly VITE_ANALYTICS_WRITE_KEY?: string;
  readonly VITE_PLAUSIBLE_API_HOST?: string;
}

interface ImportMeta {
  readonly env: ImportMetaEnv;
}

declare module "*.css" {
  const content: any;
  export default content;
}
