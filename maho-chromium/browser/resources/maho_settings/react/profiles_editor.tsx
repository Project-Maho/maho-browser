import React, {useCallback, useEffect, useRef, useState, useSyncExternalStore} from 'react';

import {Alert, AlertDescription, AlertTitle} from '@ui/alert';
import {Badge} from '@ui/badge';
import {Button} from '@ui/button';
import {Input} from '@ui/input';
import {Select, SelectContent, SelectItem, SelectTrigger, SelectValue} from '@ui/select';
import {Skeleton} from '@ui/skeleton';
import {Switch} from '@ui/switch';
import {Dialog, DialogContent, DialogHeader, DialogTitle, DialogDescription, DialogFooter} from '@ui/dialog';
import {AlertCircle, FolderOpen, Trash2} from '@icons/lucide';

import {
  ProfileLifecycleState,
} from '../maho_settings.mojom-webui.js';
import type {
  ProfileInfo,
} from '../maho_settings.mojom-webui.js';
import {scopeApplicabilityLabel} from '../models.js';
import {
  validateProfileName,
} from './profile_validation.js';
import type {MahoSettingsStore} from './store.js';

const ARCHIVE_OPTIONS = [
  {label: 'Disabled', value: 0},
  {label: 'After 12 hours', value: 12},
  {label: 'After 1 day', value: 24},
  {label: 'After 1 week', value: 168},
  {label: 'After 30 days', value: 720},
  {label: 'After 60 days', value: 1440},
  {label: 'After 90 days', value: 2160},
] as const;

function useStore(store: MahoSettingsStore) {
  return useSyncExternalStore(
      listener => store.subscribe(listener), () => store.getSnapshot());
}

function ScopeLabel() {
  return <span className="text-xs font-medium text-muted-foreground">{scopeApplicabilityLabel('core-global')}</span>;
}

function ProfileAvatar({color, name}: {color: string; name: string}) {
  return (
    <span
      aria-hidden="true"
      className="flex size-10 shrink-0 items-center justify-center rounded-xl text-sm font-semibold text-white shadow-sm"
      style={{backgroundColor: color}}>
      {(name.trim()[0] || '?').toLocaleUpperCase()}
    </span>
  );
}

function Section({title, description, children}: {title: string; description?: string; children: React.ReactNode}) {
  return (
    <section className="space-y-3">
      <div className="px-1">
        <h3 className="text-base font-semibold tracking-tight">{title}</h3>
        {description ? <p className="mt-1 text-sm text-muted-foreground">{description}</p> : null}
      </div>
      <div className="overflow-hidden rounded-xl border border-border/60 glass">{children}</div>
    </section>
  );
}

function Row({title, description, error, children}: {
  title: string;
  description: React.ReactNode;
  error?: string | null;
  children: React.ReactNode;
}) {
  return (
    <div className="grid gap-4 border-t border-border px-4 py-4 first:border-t-0 sm:grid-cols-[minmax(0,1fr)_minmax(270px,344px)] sm:px-6 sm:items-start">
      <div className="min-w-0">
        <h4 className="text-sm font-medium text-foreground">{title}</h4>
        <div className="mt-1 text-xs leading-5 text-muted-foreground">{description}</div>
        {error ? <p className="mt-1 text-xs text-destructive" role="alert">{error}</p> : null}
      </div>
      <div className="flex w-full flex-wrap items-center gap-2 sm:justify-end">{children}</div>
    </div>
  );
}

export function ProfilesEditor({store}: {store: MahoSettingsStore}) {
  const handler = store.getHandler();
  const selected = useStore(store);
  const [catalog, setCatalog] = useState<ProfileInfo[] | null>(null);
  const [catalogError, setCatalogError] = useState<string | null>(null);
  const [createName, setCreateName] = useState('');
  const [createError, setCreateError] = useState<string | null>(null);
  const [createOpen, setCreateOpen] = useState(false);
  const [catalogBusy, setCatalogBusy] = useState(false);
  const [deleteTarget, setDeleteTarget] = useState<ProfileInfo | null>(null);
  const [deleteError, setDeleteError] = useState<string | null>(null);
  const editorHeadingRef = useRef<HTMLHeadingElement>(null);

  const loadCatalog = useCallback(async () => {
    setCatalogError(null);
    try {
      const {profiles} = await handler.getProfiles();
      setCatalog(profiles);
      const initialTarget = profiles.find(candidate => !candidate.isActive);
      if (!store.getSnapshot().selectedProfileTarget && initialTarget) {
        void store.selectProfileFromCatalog(initialTarget.id);
      }
    } catch (error) {
      setCatalogError(error instanceof Error ? error.message : 'Failed to load profiles.');
      setCatalog(null);
    }
  }, [handler, store]);

  useEffect(() => { void loadCatalog(); }, [loadCatalog]);

  useEffect(() => {
    if (!selected.selectedProfileContext || selected.selectedProfileLoading || selected.selectedProfileError) return;
    void Promise.all([
      store.refreshSelectedProfileMetadata(),
      store.refreshSelectedProfileSearch(),
      store.refreshSelectedProfileDownload(),
      store.refreshSelectedProfileArchive(),
    ]);
    editorHeadingRef.current?.focus();
  }, [selected.selectedProfileContext?.profileId, selected.selectedProfileContext?.contextRevision, selected.selectedProfileError, selected.selectedProfileLoading, store]);

  const selectedInfo = catalog?.find(profile =>
    profile.id === selected.selectedProfileTarget?.profileId) ?? null;

  const selectProfile = (profile: ProfileInfo) => {
    void store.selectProfileFromCatalog(profile.id);
  };

  return (
    <div className="space-y-6">
      <Section title="Browser profiles" description="Choose a profile to edit without switching the active browser window.">
        <div className="grid gap-2 p-3 sm:grid-cols-2" role="listbox" aria-label="Profiles">
          {catalog === null && !catalogError ? [0, 1].map(index => <Skeleton key={index} className="h-16 rounded-xl" />) : null}
          {catalogError ? (
            <div className="col-span-full flex items-center justify-between gap-4 rounded-lg border border-destructive/40 p-4">
              <p className="text-sm text-destructive">{catalogError}</p>
              <Button type="button" variant="outline" onClick={() => void loadCatalog()}>Retry</Button>
            </div>
          ) : null}
          {catalog?.map(profile => {
            const editing = profile.id === selected.selectedProfileTarget?.profileId;
            return (
              <button
                aria-selected={editing}
                className="flex min-w-0 items-center gap-3 rounded-xl border border-border/70 bg-background/50 p-3 text-left transition-colors hover:bg-surface-hover aria-selected:border-primary aria-selected:bg-primary/5"
                data-profile-name={profile.name}
                key={profile.id}
                role="option"
                type="button"
                onClick={() => selectProfile(profile)}>
                <ProfileAvatar color={editing && selected.selectedProfileMetadata?.avatarColor || '#007AFF'} name={profile.name} />
                <span className="min-w-0 flex-1">
                  <span className="block truncate text-sm font-medium">{profile.name}</span>
                  <span className="mt-1 flex flex-wrap gap-1">
                    {profile.isActive ? <Badge variant="secondary">This browser window</Badge> : null}
                    {editing ? <Badge>Editing</Badge> : null}
                    {profile.isDefault ? <Badge variant="outline">Default</Badge> : null}
                  </span>
                </span>
              </button>
            );
          })}
        </div>
        <div className="flex items-center justify-between px-3 pb-1">
          <ScopeLabel />
          <Button type="button" variant="outline" onClick={() => { setCreateName(''); setCreateError(null); setCreateOpen(true); }}>Add profile</Button>
        </div>
      </Section>

      <Dialog open={createOpen} onOpenChange={open => { setCreateOpen(open); if (!open) setCreateError(null); }}>
        <DialogContent>
          <DialogHeader>
            <DialogTitle>Add profile</DialogTitle>
            <DialogDescription>Create another browser profile without switching the active browser window.</DialogDescription>
          </DialogHeader>
          <div className="space-y-2">
            <Input aria-label="New profile name" disabled={catalogBusy} placeholder="New profile name" value={createName} onChange={event => setCreateName(event.currentTarget.value)} />
            {createError ? <p className="text-sm text-destructive">{createError}</p> : null}
          </div>
          <DialogFooter>
            <Button type="button" variant="outline" disabled={catalogBusy} onClick={() => setCreateOpen(false)}>Cancel</Button>
            <Button type="button" disabled={catalogBusy} onClick={async () => {
              const error = validateProfileName(createName, catalog ?? [], '');
              if (error) { setCreateError(error); return; }
              setCatalogBusy(true);
              setCreateError(null);
              try {
                const {profile} = await handler.createProfile(createName.trim());
                if (!profile) { setCreateError('The browser rejected profile creation. The name may already be in use.'); return; }
                setCreateName('');
                setCreateOpen(false);
                await loadCatalog();
                await store.selectProfileFromCatalog(profile.id);
              } catch (error) {
                setCreateError(error instanceof Error ? error.message : 'Profile creation failed.');
              } finally { setCatalogBusy(false); }
            }}>{catalogBusy ? 'Creating…' : 'Create'}</Button>
          </DialogFooter>
        </DialogContent>
      </Dialog>

      {selected.selectedProfileLoading ? (
        <div className="space-y-3" aria-label="Loading selected profile">
          <Skeleton className="h-24 rounded-xl" />
          <Skeleton className="h-64 rounded-xl" />
        </div>
      ) : selected.selectedProfileError ? (
        <Alert variant="destructive">
          <AlertCircle className="size-4" />
          <AlertTitle>Profile unavailable</AlertTitle>
          <AlertDescription className="mt-2 flex flex-wrap items-center gap-3">
            <span>{selected.selectedProfileError}</span>
            {selected.selectedProfileTarget ? <Button type="button" variant="outline" onClick={() => {
              const target = selected.selectedProfileTarget!;
              if (target.targetToken) void store.selectProfileTarget(target);
              else void store.selectProfileFromCatalog(target.profileId);
            }}>Retry</Button> : null}
          </AlertDescription>
        </Alert>
      ) : selected.selectedProfileContext ? (
        <ProfileDetail
          catalog={catalog ?? []}
          deleteError={deleteError}
          deleteTarget={deleteTarget}
          editorHeadingRef={editorHeadingRef}
          profile={selectedInfo}
          store={store}
          onCancelDelete={() => { setDeleteTarget(null); setDeleteError(null); }}
          onDelete={async profile => {
            if (profile.isDefault) { setDeleteError('The default profile cannot be deleted.'); return; }
            if ((catalog?.length ?? 0) <= 1) { setDeleteError('The final profile cannot be deleted.'); return; }
            if (profile.spaceIds.length > 0) { setDeleteError('This profile is referenced by existing Spaces and cannot be deleted.'); return; }
            if (profile.isActive) { setDeleteError('The profile hosting this browser window cannot be deleted here.'); return; }
            setCatalogBusy(true);
            const {success} = await handler.deleteProfile(profile.id);
            setCatalogBusy(false);
            if (!success) { setDeleteError('The browser rejected deletion because this profile is protected or in use.'); return; }
            setDeleteTarget(null);
            setDeleteError(null);
            await loadCatalog();
            const fallback = catalog?.find(item => item.id !== profile.id);
            if (fallback) await store.selectProfileFromCatalog(fallback.id);
          }}
          onRequestDelete={profile => { setDeleteTarget(profile); setDeleteError(null); }}
        />
      ) : (
        <div className="rounded-xl border border-border/70 bg-background/50 p-6 text-sm text-muted-foreground" data-profile-editor-empty="true">Select a profile above to edit its settings without switching the active browser window.</div>
      )}
    </div>
  );
}

function ProfileDetail({catalog, deleteError, deleteTarget, editorHeadingRef, profile, store, onCancelDelete, onDelete, onRequestDelete}: {
  catalog: ProfileInfo[];
  deleteError: string | null;
  deleteTarget: ProfileInfo | null;
  editorHeadingRef: React.RefObject<HTMLHeadingElement | null>;
  profile: ProfileInfo | null;
  store: MahoSettingsStore;
  onCancelDelete(): void;
  onDelete(profile: ProfileInfo): Promise<void>;
  onRequestDelete(profile: ProfileInfo): void;
}) {
  const state = useStore(store);
  const metadata = state.selectedProfileMetadata;
  const search = state.selectedProfileSearch;
  const download = state.selectedProfileDownload;
  const archive = state.selectedProfileArchive;
  const deleting = state.selectedProfileContext?.lifecycleState === ProfileLifecycleState.kDeleting;
  const disabled = deleting || state.selectedProfileSavingKeys.size > 0;
  const [nameDraft, setNameDraft] = useState(metadata?.name ?? profile?.name ?? '');
  const [nameError, setNameError] = useState<string | null>(null);

  useEffect(() => { setNameDraft(metadata?.name ?? profile?.name ?? ''); }, [metadata?.name, profile?.id]);

  const mutationError = state.selectedProfileMutationError?.message ?? null;

  return (
    <div className="space-y-6" data-profile-editor="true">
      <div className="flex items-center gap-4 rounded-2xl border border-border/70 bg-card p-5 shadow-sm">
        <ProfileAvatar color={metadata?.avatarColor ?? '#007AFF'} name={nameDraft} />
        <div className="min-w-0 flex-1">
          <h3 className="truncate text-lg font-semibold" ref={editorHeadingRef} tabIndex={-1}>{nameDraft || 'Selected profile'}</h3>
          <div className="mt-1 flex flex-wrap gap-1">
            {profile?.isActive ? <Badge variant="secondary">This browser window</Badge> : null}
            <Badge>Editing</Badge>
            {profile?.isDefault ? <Badge variant="outline">Default</Badge> : null}
          </div>
        </div>
      </div>

      {deleting ? <Alert><AlertCircle className="size-4" /><AlertTitle>Profile deletion is in progress</AlertTitle><AlertDescription>Editing is disabled until deletion finishes.</AlertDescription></Alert> : null}
      {mutationError ? <Alert variant="destructive"><AlertCircle className="size-4" /><AlertTitle>Could not save this profile</AlertTitle><AlertDescription>{mutationError}</AlertDescription></Alert> : null}

      <Section title="Identity" description="Profile-scoped name.">
        <Row title="Profile name" description="Shown in profile selectors and browser surfaces." error={nameError}>
          <Input aria-label="Profile name" disabled={disabled || !metadata} value={nameDraft} onChange={event => setNameDraft(event.currentTarget.value)} />
          <Button disabled={disabled || !metadata} type="button" onClick={async () => {
            const error = validateProfileName(nameDraft, catalog, profile?.id ?? '');
            setNameError(error); if (error) return;
            await store.updateSelectedProfileMetadata({name: nameDraft.trim(), avatarColor: metadata?.avatarColor ?? '#007AFF'});
          }}>{state.selectedProfileSavingKeys.has('metadata') ? 'Saving…' : 'Save'}</Button>
        </Row>
      </Section>

      <Section title="Search" description="Default search provider and suggestion behavior.">
        <Row title="Default search engine" description={search ? 'Choose from engines available to this profile.' : 'Search settings are unavailable.'}><EnumSelect disabled={disabled || !search} label="Default search engine" value={search?.engines.find(engine => engine.isDefault)?.keyword ?? ''} options={(search?.engines ?? []).map(engine => [engine.keyword, engine.name])} onChange={value => store.setSelectedProfileDefaultSearchEngine(value)} /></Row>
        <Row title="Search suggestions" description="Show query and URL suggestions while typing."><ProfileToggle disabled={disabled || !search} label="Search suggestions" value={search?.suggestionsEnabled ?? false} onChange={value => store.setSelectedProfileSearchSuggestionsEnabled(value)} /></Row>
      </Section>

      <Section title="Downloads" description="The browser owns directory selection; paths cannot be typed here.">
        <Row title="Download directory" description={download?.directoryDisplayPath || 'Directory unavailable.'}><Button disabled={disabled || !download} type="button" variant="outline" onClick={() => store.selectSelectedProfileDownloadDirectory()}><FolderOpen className="size-4" aria-hidden="true" /> Choose folder</Button></Row>
        <Row title="Ask where to save each file" description="Prompt for a location before every download."><ProfileToggle disabled={disabled || !download} label="Ask where to save each file" value={download?.promptForDownload ?? false} onChange={value => store.setSelectedProfileDownloadPrompt(value)} /></Row>
      </Section>

      <Section title="Archive" description="Automatically archive inactive tabs for this profile.">
        <Row title="Archive timeout" description={archive ? 'Choose when inactive tabs are archived.' : 'Archive settings are unavailable in this build.'}><EnumSelect disabled={disabled || !archive} label="Archive timeout" value={archive?.timeoutHours ?? 0} options={ARCHIVE_OPTIONS.map(option => [option.value, option.label])} onChange={value => store.setSelectedProfileArchiveTimeout(value)} /></Row>
      </Section>

      {profile ? <Section title="Profile lifecycle" description="Destructive actions remain bound to the current browser context.">
        {deleteTarget?.id === profile.id ? <Row title={`Delete ${profile.name}?`} description="This action permanently removes the profile. The browser will reject protected, final, active, or referenced profiles." error={deleteError}><Button type="button" variant="outline" onClick={onCancelDelete}>Cancel</Button><Button type="button" variant="destructive" onClick={() => void onDelete(profile)}>Confirm delete</Button></Row> : <Row title="Delete profile" description="Remove this profile after browser lifecycle checks."><Button aria-label={`Delete profile ${profile.name}`} disabled={deleting} type="button" variant="destructive" onClick={() => onRequestDelete(profile)}><Trash2 className="size-4" aria-hidden="true" /> Delete</Button></Row>}
      </Section> : null}
    </div>
  );
}

function ProfileToggle({disabled, label, value, onChange}: {disabled: boolean; label: string; value: boolean; onChange(value: boolean): Promise<boolean>}) {
  return <Switch aria-label={label} checked={value} disabled={disabled} onCheckedChange={next => void onChange(next)} />;
}

function EnumSelect<T extends string | number>({disabled, label, value, options, onChange}: {disabled: boolean; label: string; value: T; options: ReadonlyArray<readonly [T, string]>; onChange(value: T): Promise<boolean>}) {
  return (
    <Select disabled={disabled} value={String(value)} onValueChange={next => { const option = options.find(([candidate]) => String(candidate) === next); if (option) void onChange(option[0]); }}>
      <SelectTrigger aria-label={label} className="w-full bg-background/70"><SelectValue placeholder={label} /></SelectTrigger>
      <SelectContent>{options.map(([optionValue, optionLabel]) => <SelectItem key={String(optionValue)} value={String(optionValue)}>{optionLabel}</SelectItem>)}</SelectContent>
    </Select>
  );
}
