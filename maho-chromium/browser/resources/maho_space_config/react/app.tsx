import React, {useEffect, useMemo, useRef, useState} from 'react';
import {createRoot} from 'react-dom/client';

import {
  InitialFocus,
  PageCallbackRouter,
  PageHandlerFactory,
  PageHandlerRemote,
} from '../mojo.js';
import type {
  ProfileInfo,
  SpaceInfo,
} from '../maho_space_config.mojom-webui.js';
import {EMOJI_CATEGORIES} from './emoji_data.js';
import {cn} from '@lib/utils';
import {User} from '@icons/lucide';
import {applyTheme, watchAutoTheme} from '@theme/apply_theme';
import {Button} from '@ui/button';
import {
  Card,
  CardContent,
  CardDescription,
  CardTitle,
} from '@ui/card';
import {Input} from '@ui/input';
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@ui/select';
import {ToggleGroup, ToggleGroupItem} from '@ui/toggle-group';

const FALLBACK_ICON = '⊞';
const DEFAULT_PROFILE_VALUE = '__maho_default_profile__';
const SURFACE_CARD_CLASS =
    'rounded-xl border-border/80 bg-card/80 shadow-none backdrop-blur-sm transition-colors focus-within:border-primary/60';

function findCategoryForIcon(icon: string): string {
  if (!icon) {
    return EMOJI_CATEGORIES[0]?.id ?? '';
  }
  for (const category of EMOJI_CATEGORIES) {
    if (category.emojis.some((entry) => entry.e === icon)) {
      return category.id;
    }
  }
  return EMOJI_CATEGORIES[0]?.id ?? '';
}

function resolveProfileValue(
    profiles: ProfileInfo[], currentProfileId: string | null): string {
  const normalizedProfileId = currentProfileId?.trim() ?? '';
  const defaultProfileId =
      profiles.find((profile) => profile.isDefault)?.id ?? '';
  if (!normalizedProfileId ||
      normalizedProfileId.toLowerCase() === 'default' ||
      normalizedProfileId === defaultProfileId) {
    return DEFAULT_PROFILE_VALUE;
  }

  return profiles.some(
      (profile) => !profile.isDefault && profile.id === normalizedProfileId) ?
      normalizedProfileId :
      DEFAULT_PROFILE_VALUE;
}

function SpaceConfigApp(
    {handler, initialFocus, initialInfo}: {
      handler: PageHandlerRemote;
      initialFocus: InitialFocus;
      initialInfo: SpaceInfo;
    }) {
  const [nameInputValue, setNameInputValue] = useState(initialInfo.name);
  const [spaceIcon, setSpaceIcon] = useState(initialInfo.icon || FALLBACK_ICON);
  const [searchQuery, setSearchQuery] = useState('');
  const [activeCategoryId, setActiveCategoryId] = useState(() =>
      findCategoryForIcon(initialInfo.icon || ''));
  const [profiles, setProfiles] = useState<ProfileInfo[]>([]);
  const [profileValue, setProfileValue] = useState(DEFAULT_PROFILE_VALUE);
  const [saving, setSaving] = useState(false);
  const [saveError, setSaveError] = useState('');

  const nameInputRef = useRef<HTMLInputElement>(null);
  const searchInputRef = useRef<HTMLInputElement>(null);
  const profileTriggerRef = useRef<HTMLButtonElement>(null);

  const spaceName = useMemo(() => nameInputValue.trim(), [nameInputValue]);
  const normalizedQuery = useMemo(
      () => searchQuery.trim().toLowerCase(), [searchQuery]);
  const showCategoryTabs = normalizedQuery.length === 0;

  const visibleEmojis = useMemo(() => {
    if (normalizedQuery) {
      return EMOJI_CATEGORIES.flatMap(
          category => category.emojis.filter(
              entry => entry.k.includes(normalizedQuery) ||
                  entry.e === normalizedQuery));
    }

    const activeCategory = EMOJI_CATEGORIES.find(
        category => category.id === activeCategoryId);
    return activeCategory?.emojis ?? [];
  }, [activeCategoryId, normalizedQuery]);

  useEffect(() => {
    applyTheme('auto');
    return watchAutoTheme((resolved) => applyTheme(resolved));
  }, []);

  useEffect(() => {
    let cancelled = false;

    void handler.getProfiles().then(({profiles}) => {
      if (cancelled) {
        return;
      }

      setProfiles(profiles);
      setProfileValue(resolveProfileValue(profiles, initialInfo.profileId || null));
    });

    return () => {
      cancelled = true;
    };
  }, [handler, initialInfo.profileId]);

  useEffect(() => {
    const frame = window.requestAnimationFrame(() => {
      switch (initialFocus) {
        case InitialFocus.kName:
          nameInputRef.current?.focus();
          nameInputRef.current?.select();
          break;
        case InitialFocus.kIcon:
          searchInputRef.current?.focus();
          break;
        case InitialFocus.kProfile:
          profileTriggerRef.current?.focus();
          break;
        case InitialFocus.kColor:
        default:
          break;
      }
    });

    return () => window.cancelAnimationFrame(frame);
  }, [initialFocus]);

  const handleCancel = () => {
    handler.closeDialog();
  };

  const handleSave = async () => {
    if (saving) {
      return;
    }

    setSaving(true);
    setSaveError('');
    try {
      const selectedProfileId =
          profileValue === DEFAULT_PROFILE_VALUE ? '' : profileValue;
      const nameChanged = spaceName !== initialInfo.name;
      const iconChanged = spaceIcon !== initialInfo.icon;
      const profileChanged = profileValue !==
          resolveProfileValue(profiles, initialInfo.profileId || null);

      const promises: Array<Promise<{success: boolean}>> = [];

      if (nameChanged && spaceName) {
        promises.push(handler.updateName(spaceName));
      }

      if (iconChanged) {
        promises.push(handler.updateIcon(spaceIcon));
      }

      if (profileChanged) {
        promises.push(handler.updateProfile(selectedProfileId || null));
      }

      if (promises.length > 0) {
        const results = await Promise.all(promises);
        if (!results.every((result) => result.success)) {
          setSaveError('Could not save this Space. Please try again.');
          return;
        }
      }

      handler.closeDialog();
    } catch {
      setSaveError('Could not save this Space. Please try again.');
    } finally {
      setSaving(false);
    }
  };

  return (
    <div
      id="app"
      className="flex h-full w-full min-w-0 flex-col overflow-hidden bg-background text-foreground">
      <div className="flex h-full min-h-0 flex-1 flex-col gap-3.5 p-5">
        <Card className={SURFACE_CARD_CLASS}>
          <CardContent className="p-4">
            <div className="flex flex-col gap-1">
              <span className="text-xs font-medium text-muted-foreground">Space name</span>
              <Input
                aria-label="Space name"
                ref={nameInputRef}
                className="h-auto border-0 bg-transparent px-0 py-0 text-sm font-medium shadow-none placeholder:text-muted-foreground/60 focus-visible:ring-0"
                placeholder="Enter a name"
                type="text"
                value={nameInputValue}
                onChange={(event) => setNameInputValue(event.currentTarget.value)}
              />
            </div>
          </CardContent>
        </Card>

        <Card className={cn(SURFACE_CARD_CLASS, 'flex min-h-0 flex-1 flex-col')}>
          <CardContent className="flex min-h-0 flex-1 flex-col gap-3 p-4">
            <div className="flex items-start gap-3">
              <div className="min-w-0 flex-1">
                <CardTitle className="text-sm font-semibold">Icon</CardTitle>
                <CardDescription className="mt-1 text-xs">
                  Pick a unicode symbol
                </CardDescription>
              </div>
              <div className="flex size-11 shrink-0 items-center justify-center rounded-xl border border-border/80 bg-background/60 text-2xl shadow-inner">
                {spaceIcon || FALLBACK_ICON}
              </div>
            </div>

            <Input
              aria-label="Search emoji"
              ref={searchInputRef}
              className="h-10 rounded-lg border-border/80 bg-background/70 text-sm shadow-none placeholder:text-muted-foreground"
              placeholder="Search emoji…"
              type="search"
              value={searchQuery}
              onChange={(event) => setSearchQuery(event.currentTarget.value)}
            />

            {showCategoryTabs ? (
              <div className="overflow-x-auto pb-1">
                <ToggleGroup
                  aria-label="Emoji categories"
                  className="w-max justify-start gap-1"
                  type="single"
                  value={activeCategoryId}
                  onValueChange={(value) => {
                    if (value) {
                      setActiveCategoryId(value);
                    }
                  }}>
                  {EMOJI_CATEGORIES.map((category) => (
                    <ToggleGroupItem
                      key={category.id}
                      className="h-7 rounded-full px-3 text-xs font-medium text-muted-foreground hover:bg-muted/80 hover:text-foreground data-[state=on]:bg-primary data-[state=on]:text-primary-foreground"
                      size="sm"
                      value={category.id}>
                      {category.label}
                    </ToggleGroupItem>
                  ))}
                </ToggleGroup>
              </div>
            ) : null}

            <div className="min-h-0 flex-1 overflow-hidden rounded-lg border border-border/80 bg-background/60 p-1">
              {visibleEmojis.length === 0 ? (
                <div className="flex h-full min-h-56 items-center justify-center text-xs text-muted-foreground">
                  No matches
                </div>
              ) : (
                <div className="grid h-full min-h-0 grid-cols-10 gap-0.5 overflow-y-auto pr-0.5">
                  {visibleEmojis.map((entry, index) => {
                    const selected = entry.e === spaceIcon;
                    return (
                      <button
                        key={`${entry.e}-${index}`}
                        aria-pressed={selected}
                        className={cn(
                          'aspect-square rounded-md text-xl transition duration-100 hover:scale-[1.15] hover:bg-accent/20 focus-visible:outline-none focus-visible:ring-1 focus-visible:ring-ring',
                          selected
                            ? 'bg-primary/30 ring-1 ring-primary'
                            : 'bg-transparent',
                        )}
                        title={entry.e}
                        type="button"
                        onClick={() => setSpaceIcon(entry.e)}>
                        {entry.e}
                      </button>
                    );
                  })}
                </div>
              )}
            </div>
          </CardContent>
        </Card>

        <Card className={SURFACE_CARD_CLASS}>
          <CardContent className="p-4">
            <div className="flex items-center gap-3">
              <div className="flex size-4 shrink-0 items-center justify-center text-muted-foreground">
                <User className="size-4" />
              </div>
              <div className="min-w-0 flex-1">
                <CardTitle className="text-sm font-semibold">Profile</CardTitle>
                <CardDescription className="mt-1 text-xs">
                  Browser identity for this space
                </CardDescription>
              </div>
              <Select value={profileValue} onValueChange={setProfileValue}>
                <SelectTrigger
                  aria-label="Profile"
                  ref={profileTriggerRef}
                  className="h-8 w-36 rounded-full border-border/80 bg-background/70 text-sm shadow-none">
                  <SelectValue />
                </SelectTrigger>
                <SelectContent className="border-border/80 bg-popover/95">
                  <SelectItem value={DEFAULT_PROFILE_VALUE}>Default</SelectItem>
                  {profiles.filter((profile) => !profile.isDefault).map((profile) => (
                    <SelectItem key={profile.id} value={profile.id}>
                      {profile.name}
                    </SelectItem>
                  ))}
                </SelectContent>
              </Select>
            </div>
          </CardContent>
        </Card>

        {saveError && <p role="alert" className="text-sm text-destructive">{saveError}</p>}
        <div className="mt-auto flex gap-2 pt-1">
          <Button
            className="h-10 rounded-lg border border-border/80 px-4 text-sm text-muted-foreground hover:bg-muted/70 hover:text-foreground"
            type="button"
            variant="ghost"
            onClick={handleCancel}>
            Cancel
          </Button>
          <Button
            className="h-10 flex-1 rounded-lg text-sm font-semibold"
            disabled={saving}
            type="button"
            onClick={() => {
              void handleSave();
            }}>
            {saving ? 'Saving…' : 'Save'}
          </Button>
        </div>
      </div>
    </div>
  );
}

async function initialize(): Promise<void> {
  const callbackRouter = new PageCallbackRouter();
  const handler = new PageHandlerRemote();
  const factory = PageHandlerFactory.getRemote();

  factory.createPageHandler(
      callbackRouter.$.bindNewPipeAndPassRemote(),
      handler.$.bindNewPipeAndPassReceiver());

  const rootElement = document.querySelector('maho-space-config');
  if (!(rootElement instanceof HTMLElement)) {
    return;
  }

  document.documentElement.style.height = '100%';
  document.body.style.width = '100%';
  document.body.style.height = '100%';
  document.body.style.margin = '0';
  document.body.style.overflow = 'hidden';
  rootElement.style.display = 'block';
  rootElement.style.width = '100%';
  rootElement.style.height = '100%';

  const {info, focus} = await handler.getSpaceInfo();
  if (!info) {
    return;
  }

  createRoot(rootElement).render(
      <React.StrictMode>
        <SpaceConfigApp
          handler={handler}
          initialFocus={focus}
          initialInfo={info}
        />
      </React.StrictMode>);
}

void initialize();
