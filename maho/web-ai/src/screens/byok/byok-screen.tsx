/**
 * BYOK Settings screen — Phase A reference implementation.
 *
 * Mirrors native:
 *   Android: BYOKSettingsScreen.kt
 *   iOS:     BYOKSettingsView.swift
 *
 * Bridge calls used:
 *   byokGetProviders() → string[]
 *   byokGetKey(provider) → string | null
 *   byokSetKey(provider, key) → boolean
 *   byokDeleteKey(provider) → boolean
 *   byokValidateKey(provider, key) → boolean
 */
import { useCallback, useEffect, useReducer } from 'preact/hooks';
import { useBridge } from '../../hooks/use-bridge';
import { Button } from '../../ui/button';
import { Icon } from '../../ui/icon';
import { Input } from '../../ui/input';
import { ListItem } from '../../ui/list-item';
import { StatusBanner } from '../../ui/status-banner';
import { clsx } from '../../utils/clsx';
import { ProviderCard } from './provider-card';
import type { ValidationPhase, ValidationStatus } from './provider-card';

// ─── Provider catalogue ──────────────────────────────────────────────────────

interface ProviderMeta {
  id: string;
  displayName: string;
  keyPlaceholder: string;
  helperText: string;
}

const KNOWN_PROVIDERS: ProviderMeta[] = [
  {
    id: 'openai',
    displayName: 'OpenAI',
    keyPlaceholder: 'sk-...',
    helperText: 'Used for OpenAI-powered browsing and AI features.',
  },
  {
    id: 'anthropic',
    displayName: 'Anthropic',
    keyPlaceholder: 'sk-ant-...',
    helperText: 'Used for Claude-powered browsing and AI features.',
  },
];

function metaForId(id: string): ProviderMeta {
  return (
    KNOWN_PROVIDERS.find((p) => p.id === id) ?? {
      id,
      displayName: id,
      keyPlaceholder: '',
      helperText: '',
    }
  );
}

function maskApiKey(key: string): string {
  const suffix = key.slice(-Math.min(4, key.length));
  const maskLen = Math.max(8, key.length - suffix.length);
  return '•'.repeat(maskLen) + suffix;
}

// ─── State ───────────────────────────────────────────────────────────────────

interface State {
  loading: boolean;
  /** provider id → stored key */
  savedKeys: Record<string, string>;
  /** provider id → validation status */
  validationStatuses: Record<string, ValidationStatus>;
  errorMessage: string | null;
  showAddSheet: boolean;
  draftProviderId: string;
  draftKey: string;
  draftValidation: ValidationStatus;
  savingDraft: boolean;
  saveError: string | null;
}

type Action =
  | { type: 'LOAD_START' }
  | { type: 'LOAD_DONE'; savedKeys: Record<string, string> }
  | { type: 'LOAD_ERROR'; message: string }
  | { type: 'OPEN_ADD_SHEET'; defaultProviderId: string }
  | { type: 'CLOSE_ADD_SHEET' }
  | { type: 'DRAFT_PROVIDER'; id: string }
  | { type: 'DRAFT_KEY'; key: string }
  | { type: 'DRAFT_VALIDATE_START' }
  | { type: 'DRAFT_VALIDATE_DONE'; valid: boolean }
  | { type: 'DRAFT_SAVE_START' }
  | { type: 'DRAFT_SAVE_DONE'; providerId: string; key: string }
  | { type: 'DRAFT_SAVE_ERROR'; message: string }
  | { type: 'VALIDATE_START'; providerId: string }
  | { type: 'VALIDATE_DONE'; providerId: string; valid: boolean }
  | { type: 'DELETE_DONE'; providerId: string }
  | { type: 'DELETE_ERROR'; message: string };

function initialState(): State {
  const defaultProvider = KNOWN_PROVIDERS[0]?.id ?? '';
  return {
    loading: true,
    savedKeys: {},
    validationStatuses: {},
    errorMessage: null,
    showAddSheet: false,
    draftProviderId: defaultProvider,
    draftKey: '',
    draftValidation: { phase: 'idle' },
    savingDraft: false,
    saveError: null,
  };
}

function reducer(state: State, action: Action): State {
  switch (action.type) {
    case 'LOAD_START':
      return { ...state, loading: true, errorMessage: null };
    case 'LOAD_DONE':
      return { ...state, loading: false, savedKeys: action.savedKeys };
    case 'LOAD_ERROR':
      return { ...state, loading: false, errorMessage: action.message };

    case 'OPEN_ADD_SHEET':
      return {
        ...state,
        showAddSheet: true,
        draftProviderId: action.defaultProviderId,
        draftKey: '',
        draftValidation: { phase: 'idle' },
        savingDraft: false,
        saveError: null,
      };
    case 'CLOSE_ADD_SHEET':
      return {
        ...state,
        showAddSheet: false,
        draftKey: '',
        draftValidation: { phase: 'idle' },
        savingDraft: false,
        saveError: null,
      };

    case 'DRAFT_PROVIDER':
      return {
        ...state,
        draftProviderId: action.id,
        draftKey: '',
        draftValidation: { phase: 'idle' },
        saveError: null,
      };
    case 'DRAFT_KEY':
      return {
        ...state,
        draftKey: action.key,
        draftValidation: { phase: 'idle' },
        saveError: null,
      };

    case 'DRAFT_VALIDATE_START':
      return {
        ...state,
        draftValidation: {
          phase: 'checking',
          message: `Checking ${metaForId(state.draftProviderId).displayName} key…`,
        },
      };
    case 'DRAFT_VALIDATE_DONE':
      return {
        ...state,
        draftValidation: action.valid
          ? { phase: 'valid', message: 'Validated. Save to keep using this key.' }
          : { phase: 'invalid', message: 'Validation failed. Double-check the provider and key.' },
      };

    case 'DRAFT_SAVE_START':
      return { ...state, savingDraft: true, saveError: null };
    case 'DRAFT_SAVE_DONE': {
      const updatedKeys = { ...state.savedKeys, [action.providerId]: action.key };
      const updatedStatuses = {
        ...state.validationStatuses,
        [action.providerId]: { phase: 'valid' as ValidationPhase, message: 'Saved and validated.' },
      };
      return {
        ...state,
        savingDraft: false,
        showAddSheet: false,
        savedKeys: updatedKeys,
        validationStatuses: updatedStatuses,
      };
    }
    case 'DRAFT_SAVE_ERROR':
      return { ...state, savingDraft: false, saveError: action.message };

    case 'VALIDATE_START':
      return {
        ...state,
        validationStatuses: {
          ...state.validationStatuses,
          [action.providerId]: {
            phase: 'checking',
            message: `Checking ${metaForId(action.providerId).displayName} key…`,
          },
        },
      };
    case 'VALIDATE_DONE':
      return {
        ...state,
        validationStatuses: {
          ...state.validationStatuses,
          [action.providerId]: action.valid
            ? { phase: 'valid', message: 'Validated successfully.' }
            : {
                phase: 'invalid',
                message: 'Validation failed. Delete and add a fresh key if needed.',
              },
        },
      };

    case 'DELETE_DONE': {
      const { [action.providerId]: _key, ...remainingKeys } = state.savedKeys;
      const { [action.providerId]: _status, ...remainingStatuses } = state.validationStatuses;
      return {
        ...state,
        savedKeys: remainingKeys,
        validationStatuses: remainingStatuses,
        errorMessage: null,
      };
    }
    case 'DELETE_ERROR':
      return { ...state, errorMessage: action.message };

    default:
      return state;
  }
}

// ─── Component ───────────────────────────────────────────────────────────────

interface BYOKScreenProps {
  onBack?: () => void;
}

export function BYOKScreen({ onBack }: BYOKScreenProps) {
  const bridge = useBridge();
  const [state, dispatch] = useReducer(reducer, undefined, initialState);

  // ── Load saved keys on mount ──
  const loadSavedKeys = useCallback(async () => {
    dispatch({ type: 'LOAD_START' });
    try {
      const providerIds = await bridge.byokGetProviders();
      const entries: Record<string, string> = {};
      await Promise.all(
        providerIds.map(async (id) => {
          const key = await bridge.byokGetKey(id);
          if (key && key.trim()) {
            entries[id] = key.trim();
          }
        }),
      );
      dispatch({ type: 'LOAD_DONE', savedKeys: entries });
    } catch (err) {
      dispatch({
        type: 'LOAD_ERROR',
        message: err instanceof Error ? err.message : 'Failed to load saved keys.',
      });
    }
  }, [bridge]);

  useEffect(() => {
    void loadSavedKeys();
  }, [loadSavedKeys]);

  // ── Draft: validate ──
  const validateDraft = useCallback(async () => {
    const key = state.draftKey.trim();
    if (!key) return;
    dispatch({ type: 'DRAFT_VALIDATE_START' });
    try {
      const valid = await bridge.byokValidateKey(state.draftProviderId, key);
      dispatch({ type: 'DRAFT_VALIDATE_DONE', valid });
    } catch {
      dispatch({ type: 'DRAFT_VALIDATE_DONE', valid: false });
    }
  }, [bridge, state.draftProviderId, state.draftKey]);

  // ── Draft: save ──
  const saveDraft = useCallback(async () => {
    const key = state.draftKey.trim();
    if (!key || state.draftValidation.phase !== 'valid') return;
    dispatch({ type: 'DRAFT_SAVE_START' });
    try {
      const ok = await bridge.byokSetKey(state.draftProviderId, key);
      if (ok) {
        dispatch({ type: 'DRAFT_SAVE_DONE', providerId: state.draftProviderId, key });
      } else {
        dispatch({ type: 'DRAFT_SAVE_ERROR', message: 'Unable to save this key right now.' });
      }
    } catch {
      dispatch({ type: 'DRAFT_SAVE_ERROR', message: 'Unable to save this key right now.' });
    }
  }, [bridge, state.draftProviderId, state.draftKey, state.draftValidation.phase]);

  // ── Validate stored key ──
  const validateStored = useCallback(
    async (providerId: string) => {
      const key = state.savedKeys[providerId];
      if (!key) return;
      dispatch({ type: 'VALIDATE_START', providerId });
      try {
        const valid = await bridge.byokValidateKey(providerId, key);
        dispatch({ type: 'VALIDATE_DONE', providerId, valid });
      } catch {
        dispatch({ type: 'VALIDATE_DONE', providerId, valid: false });
      }
    },
    [bridge, state.savedKeys],
  );

  // ── Delete stored key ──
  const deleteKey = useCallback(
    async (providerId: string) => {
      try {
        const ok = await bridge.byokDeleteKey(providerId);
        if (ok) {
          dispatch({ type: 'DELETE_DONE', providerId });
        } else {
          dispatch({
            type: 'DELETE_ERROR',
            message: `Unable to delete the ${metaForId(providerId).displayName} key.`,
          });
        }
      } catch {
        dispatch({
          type: 'DELETE_ERROR',
          message: `Unable to delete the ${metaForId(providerId).displayName} key.`,
        });
      }
    },
    [bridge],
  );

  const configuredProviders = KNOWN_PROVIDERS.filter((p) => p.id in state.savedKeys);
  const availableProviders = KNOWN_PROVIDERS.filter((p) => !(p.id in state.savedKeys));
  const defaultDraftProvider = availableProviders[0] ?? KNOWN_PROVIDERS[0];

  return (
    <div
      class="flex flex-col min-h-screen bg-[var(--color-surface)] text-[var(--color-on-surface)]"
      data-testid="aiScreenBYOK"
    >
      {/* Top bar */}
      <header class="flex items-center gap-2 px-4 py-3 border-b border-[var(--color-border)] sticky top-0 bg-[var(--color-surface)] z-10">
        {onBack && (
          <button
            class="p-2 -ml-2 rounded-xl hover:bg-[var(--color-surface-hover)] transition-colors"
            onClick={onBack}
            aria-label="Back"
          >
            <Icon name="chevron-left" size={20} aria-hidden />
          </button>
        )}
        <h1 class="text-base font-semibold flex-1">LLM API Keys</h1>
      </header>

      {/* Scroll area */}
      <div class="flex-1 overflow-y-auto">
        {/* ── Configured providers ── */}
        <SectionHeader>Configured Providers</SectionHeader>

        {state.loading ? (
          <ListItem
            leading={<SpinnerIcon />}
            headline="Loading saved keys"
            supporting="Checking which providers already have BYOK credentials."
          />
        ) : state.errorMessage ? (
          <div class="px-4 py-3">
            <StatusBanner kind="error">{state.errorMessage}</StatusBanner>
          </div>
        ) : configuredProviders.length === 0 ? (
          <ListItem
            leading={<Icon name="key-round" size={20} aria-hidden />}
            headline="No API keys saved"
            supporting="Add an OpenAI or Anthropic key to use BYOK-powered AI features."
          />
        ) : (
          <div class="divide-y divide-[var(--color-border)]">
            {configuredProviders.map((p) => (
              <ProviderCard
                key={p.id}
                displayName={p.displayName}
                maskedKey={maskApiKey(state.savedKeys[p.id]!)}
                validationStatus={state.validationStatuses[p.id] ?? { phase: 'idle' }}
                onValidate={() => void validateStored(p.id)}
                onDelete={() => void deleteKey(p.id)}
              />
            ))}
          </div>
        )}

        <Divider />

        {/* ── Add key ── */}
        <SectionHeader>Add Key</SectionHeader>
        <div class="px-4 py-2">
          <Button
            variant="primary"
            size="md"
            disabled={availableProviders.length === 0}
            onClick={() =>
              dispatch({
                type: 'OPEN_ADD_SHEET',
                defaultProviderId: defaultDraftProvider?.id ?? '',
              })
            }
          >
            <Icon name="plus" size={16} aria-hidden />
            Add Key
          </Button>
        </div>

        <p class="px-4 py-2 text-xs text-[var(--color-on-surface-secondary)]">
          {availableProviders.length === 0
            ? 'OpenAI and Anthropic are already configured. Delete a key to replace it.'
            : `Available: ${availableProviders.map((p) => p.displayName).join(', ')}`}
        </p>
      </div>

      {/* ── Add Key sheet ── */}
      {state.showAddSheet && (
        <AddKeySheet
          availableProviders={availableProviders}
          selectedProviderId={state.draftProviderId}
          keyInput={state.draftKey}
          validation={state.draftValidation}
          saving={state.savingDraft}
          saveError={state.saveError}
          onProviderChange={(id) => dispatch({ type: 'DRAFT_PROVIDER', id })}
          onKeyChange={(key) => dispatch({ type: 'DRAFT_KEY', key })}
          onValidate={() => void validateDraft()}
          onSave={() => void saveDraft()}
          onDismiss={() => dispatch({ type: 'CLOSE_ADD_SHEET' })}
        />
      )}
    </div>
  );
}

// ─── Sub-components ──────────────────────────────────────────────────────────

function SectionHeader({ children }: { children: string }) {
  return (
    <p class="px-4 pt-4 pb-1 text-xs font-semibold uppercase tracking-wider text-[var(--color-on-surface-secondary)]">
      {children}
    </p>
  );
}

function Divider() {
  return <hr class="my-2 border-[var(--color-border)]" />;
}

function SpinnerIcon() {
  return (
    <svg
      width={16}
      height={16}
      viewBox="0 0 24 24"
      fill="none"
      stroke="currentColor"
      stroke-width="2"
      stroke-linecap="round"
      stroke-linejoin="round"
      class="animate-spin"
      aria-hidden
    >
      <path d="M12 2v4M12 18v4M4.93 4.93l2.83 2.83M16.24 16.24l2.83 2.83M2 12h4M18 12h4M4.93 19.07l2.83-2.83M16.24 7.76l2.83-2.83" />
    </svg>
  );
}

interface AddKeySheetProps {
  availableProviders: ProviderMeta[];
  selectedProviderId: string;
  keyInput: string;
  validation: ValidationStatus;
  saving: boolean;
  saveError: string | null;
  onProviderChange: (id: string) => void;
  onKeyChange: (key: string) => void;
  onValidate: () => void;
  onSave: () => void;
  onDismiss: () => void;
}

function AddKeySheet({
  availableProviders,
  selectedProviderId,
  keyInput,
  validation,
  saving,
  saveError,
  onProviderChange,
  onKeyChange,
  onValidate,
  onSave,
  onDismiss,
}: AddKeySheetProps) {
  const selectedMeta = metaForId(selectedProviderId);
  const trimmedKey = keyInput.trim();
  const isChecking = validation.phase === 'checking';
  const canSave = trimmedKey.length > 0 && validation.phase === 'valid' && !saving;

  return (
    /* Backdrop */
    <div
      class="fixed inset-0 z-50 flex flex-col justify-end bg-black/40"
      onClick={(e) => {
        if (e.target === e.currentTarget && !saving && !isChecking) onDismiss();
      }}
    >
      {/* Sheet */}
      <div class="bg-[var(--color-surface)] rounded-t-2xl shadow-xl max-h-[85dvh] overflow-y-auto">
        {/* Sheet handle */}
        <div class="flex justify-center pt-3 pb-1">
          <div class="w-10 h-1 rounded-full bg-[var(--color-border)]" />
        </div>

        {/* Sheet header */}
        <div class="flex items-center justify-between px-4 py-2 border-b border-[var(--color-border)]">
          <h2 class="text-base font-semibold">Add API Key</h2>
          <button
            class="p-2 rounded-xl hover:bg-[var(--color-surface-hover)] disabled:opacity-40 transition-colors"
            onClick={onDismiss}
            disabled={saving || isChecking}
            aria-label="Cancel"
          >
            <Icon name="x" size={18} aria-hidden />
          </button>
        </div>

        {/* Sheet body */}
        <div class="flex flex-col gap-4 px-4 py-4">
          <p class="text-sm text-[var(--color-on-surface-secondary)]">
            Pick a provider, validate the key, then save it locally for BYOK.
          </p>

          {/* Provider picker — segmented row when multiple available */}
          {availableProviders.length === 1 ? (
            <p class="text-sm">
              <span class="text-[var(--color-on-surface-secondary)]">Provider: </span>
              <span class="font-medium">{selectedMeta.displayName}</span>
            </p>
          ) : (
            <div class="flex flex-col gap-1">
              <label class="text-xs font-medium text-[var(--color-on-surface-secondary)]">
                Provider
              </label>
              <div class="flex rounded-xl overflow-hidden border border-[var(--color-border)]">
                {availableProviders.map((p) => (
                  <button
                    key={p.id}
                    class={clsx(
                      'flex-1 py-2 text-sm font-medium transition-colors',
                      selectedProviderId === p.id
                        ? 'bg-[var(--color-accent)] text-[var(--color-on-accent)]'
                        : 'bg-[var(--color-surface-raised)] text-[var(--color-on-surface)] hover:bg-[var(--color-surface-hover)]',
                    )}
                    onClick={() => onProviderChange(p.id)}
                  >
                    {p.displayName}
                  </button>
                ))}
              </div>
            </div>
          )}

          {/* Key input */}
          <Input
            label={`${selectedMeta.displayName} API Key`}
            type="password"
            value={keyInput}
            placeholder={selectedMeta.keyPlaceholder}
            autocomplete="off"
            autocorrect="off"
            autocapitalize="none"
            spellcheck={false}
            onInput={(e) => onKeyChange((e.target as HTMLInputElement).value)}
          />

          {/* Validate row */}
          <div class="flex items-center gap-3">
            <Button
              variant="secondary"
              size="sm"
              onClick={onValidate}
              disabled={!trimmedKey || saving || isChecking}
              loading={isChecking}
            >
              Validate
            </Button>
            {validation.phase !== 'idle' && !isChecking && validation.message && (
              <span
                class={clsx(
                  'text-xs',
                  validation.phase === 'valid' && 'text-[var(--color-success)]',
                  validation.phase === 'invalid' && 'text-[var(--color-error)]',
                )}
              >
                {validation.message}
              </span>
            )}
          </div>

          {saveError && <StatusBanner kind="error">{saveError}</StatusBanner>}

          {/* Save / Cancel */}
          <div class="flex gap-2 pt-2">
            <Button
              variant="ghost"
              size="md"
              class="flex-1"
              onClick={onDismiss}
              disabled={saving || isChecking}
            >
              Cancel
            </Button>
            <Button
              variant="primary"
              size="md"
              class="flex-1"
              onClick={onSave}
              disabled={!canSave}
              loading={saving}
            >
              {saving ? 'Saving…' : 'Save'}
            </Button>
          </div>
        </div>
      </div>
    </div>
  );
}
