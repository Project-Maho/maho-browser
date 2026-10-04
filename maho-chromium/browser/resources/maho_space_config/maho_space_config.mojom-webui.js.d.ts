export enum InitialFocus {
  kName = 0,
  kIcon = 1,
  kColor = 2,
  kProfile = 3,
}

export interface ProfileInfo {
  id: string;
  name: string;
  isDefault: boolean;
}

export interface SpaceInfo {
  id: string;
  name: string;
  icon: string;
  color: string;
  themeJson?: string | null;
  profileId?: string | null;
}

export interface PageHandler {
  getSpaceInfo(): Promise<{info: SpaceInfo | null; focus: InitialFocus}>;
  getProfiles(): Promise<{profiles: ProfileInfo[]}>;
  updateName(name: string): Promise<{success: boolean}>;
  updateIcon(icon: string): Promise<{success: boolean}>;
  updateColor(colorHex: string, themeJson: string | null): Promise<{success: boolean}>;
  updateProfile(profileId: string | null): Promise<{success: boolean}>;
  closeDialog(): Promise<void>;
}

export class PageCallbackRouter {
  $: {
    bindNewPipeAndPassRemote(): unknown;
  };
}

export class PageHandlerRemote implements PageHandler {
  $: {
    bindNewPipeAndPassReceiver(): unknown;
  };
  getSpaceInfo(): Promise<{info: SpaceInfo | null; focus: InitialFocus}>;
  getProfiles(): Promise<{profiles: ProfileInfo[]}>;
  updateName(name: string): Promise<{success: boolean}>;
  updateIcon(icon: string): Promise<{success: boolean}>;
  updateColor(colorHex: string, themeJson: string | null): Promise<{success: boolean}>;
  updateProfile(profileId: string | null): Promise<{success: boolean}>;
  closeDialog(): Promise<void>;
}

export class PageHandlerFactory {
  static getRemote(): PageHandlerFactory;
  createPageHandler(page: unknown, handler: unknown): void;
}
