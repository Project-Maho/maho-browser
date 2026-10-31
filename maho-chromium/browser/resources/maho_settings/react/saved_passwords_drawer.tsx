// Copyright 2026 Maho Browser. All rights reserved.

import React, {useCallback, useEffect, useRef, useState} from 'react';

import {Button} from '@ui/button';
import {
  Dialog,
  DialogContent,
  DialogDescription,
  DialogFooter,
  DialogHeader,
  DialogTitle,
} from '@ui/dialog';
import {Input} from '@ui/input';
import {Textarea} from '@ui/textarea';
import {Slider} from '@ui/slider';
import {Switch} from '@ui/switch';
import {
  Check,
  Copy,
  FileText,
  Key,
  KeyRound,
  Plus,
  RefreshCw,
  Sparkles,
  Trash2,
  X,
} from 'lucide-react';
import {VaultItemKind} from '../mojo.js';
import type {
  GeneratedPasswordResult,
  PasswordGeneratorOptions,
  VaultItem,
} from '../mojo.js';
import {getDisplayName, getPrimaryOrigin} from './saved_passwords_model.js';
import type {
  AddSavedPasswordInput,
  AddSecureNoteInput,
  EditSavedPasswordInput,
  EditSecureNoteInput,
} from './saved_passwords_model.js';
import type {MahoSettingsStore} from './store.js';

const DRAWER_CONTENT_CLASS =
    'left-auto right-0 top-0 h-full max-w-md translate-x-0 translate-y-0 rounded-none border-y-0 border-r-0 glass-strong sm:rounded-none data-[state=closed]:slide-out-to-right data-[state=open]:slide-in-from-right overflow-y-auto';
const FIELD_GROUP_CLASS = 'grid gap-2';
const FIELD_LABEL_CLASS = 'text-xs font-medium text-foreground';
const FIELD_HELP_CLASS = 'text-xs leading-5 text-muted-foreground';
const FORM_CLASS = 'grid gap-4 py-2';
const ERROR_CLASS = 'text-sm leading-6 text-destructive';

export type PasswordGeneratorProps = {
  readonly onUsePassword?: (password: string) => void;
  readonly store?: MahoSettingsStore;
};

export function PasswordGenerator({onUsePassword, store}: PasswordGeneratorProps) {
  const [options, setOptions] = useState<PasswordGeneratorOptions>({
    length: 16,
    useLowercase: true,
    useUppercase: true,
    useDigits: true,
    useSymbols: true,
    avoidAmbiguous: false,
    passphraseMode: false,
    wordCount: 4,
    separator: '-',
    capitalize: true,
    includeNumber: true,
  });
  const [generatedPassword, setGeneratedPassword] = useState('');
  const [strengthScore, setStrengthScore] = useState(3);
  const [entropyBits, setEntropyBits] = useState(64);
  const [copied, setCopied] = useState(false);

  const [generatorError, setGeneratorError] = useState('');

  // The browser-side Rust CSPRNG is the only password source. A renderer-side
  // fallback would silently produce weaker passwords, so failures surface as errors.
  const generate = useCallback(async (opts: PasswordGeneratorOptions) => {
    try {
      const handler = store?.getHandler();
      if (handler) {
        const payload: PasswordGeneratorOptions = {
          ...opts,
          mode: opts.passphraseMode ? 'passphrase' : 'password',
          includeLowercase: opts.useLowercase,
          includeUppercase: opts.useUppercase,
          includeDigits: opts.useDigits,
          includeSymbols: opts.useSymbols,
        };
        const {result} = await handler.generatePassword(payload);
        if (result.success && result.password) {
          setGeneratedPassword(result.password);
          setStrengthScore(result.strengthScore);
          setEntropyBits(result.entropyBits);
          setGeneratorError('');
          return;
        }
      }
    } catch {
      // Reported below; no local generation fallback.
    }
    setGeneratedPassword('');
    setGeneratorError('Password generation is unavailable. Try again.');
  }, [store]);

  useEffect(() => {
    void generate(options);
  }, [generate, options]);

  const copyPassword = async () => {
    if (!generatedPassword) return;
    try {
      await navigator.clipboard.writeText(generatedPassword);
      setCopied(true);
      window.setTimeout(() => setCopied(false), 2000);
    } catch {
    }
  };

  const getScoreLabel = (score: number) => {
    switch (score) {
      case 4: return {label: 'Excellent', color: 'bg-success text-success'};
      case 3: return {label: 'Strong', color: 'bg-primary text-primary'};
      case 2: return {label: 'Fair', color: 'bg-warning text-warning'};
      case 1: return {label: 'Weak', color: 'bg-orange-500 text-orange-400'};
      default: return {label: 'Very weak', color: 'bg-destructive text-destructive'};
    }
  };

  const scoreInfo = getScoreLabel(strengthScore);

  return (
    <div className="grid gap-3 rounded-lg border border-border bg-background/40 p-3.5 text-xs">
      <div className="flex items-center justify-between gap-2">
        <span className="font-semibold text-foreground">Password Generator</span>
        <div className="flex items-center gap-1.5">
          <Button
            aria-label="Regenerate password"
            className="size-7 p-0"
            size="icon"
            type="button"
            variant="ghost"
            onClick={() => void generate(options)}>
            <RefreshCw className="size-3.5" />
          </Button>
          <Button
            aria-label="Copy generated password"
            className="size-7 p-0"
            size="icon"
            type="button"
            variant="ghost"
            onClick={() => void copyPassword()}>
            {copied ? <Check className="size-3.5 text-success" /> : <Copy className="size-3.5" />}
          </Button>
        </div>
      </div>

      <div className="flex items-center justify-between gap-2 rounded-md border border-border bg-background/60 px-2.5 py-2 font-mono text-xs tracking-wider text-foreground select-all break-all">
        <span data-testid="generated-password-preview">{generatedPassword || (generatorError ? '' : 'Generating…')}</span>
      </div>
      <div role="status" className={generatorError ? 'text-xs leading-5 text-destructive' : 'sr-only'}>
        {generatorError}
      </div>

      <div className="grid gap-1">
        <div className="flex items-center justify-between text-[11px]">
          <span className="text-muted-foreground">Strength: <strong className={scoreInfo.color}>{scoreInfo.label}</strong></span>
          <span className="text-muted-foreground tabular-nums">~{entropyBits} bits entropy</span>
        </div>
        <div className="flex h-1.5 w-full gap-1 overflow-hidden rounded-full bg-surface-selected">
          {[0, 1, 2, 3].map(step => (
            <div
              key={step}
              className={`h-full flex-1 transition-colors ${step <= strengthScore - 1 ? scoreInfo.color.split(' ')[0] : 'bg-transparent'}`}
            />
          ))}
        </div>
      </div>

      <div className="flex rounded-md border border-border bg-background/60 p-0.5">
        <button
          className={`flex-1 rounded py-1 text-center font-medium ${!options.passphraseMode ? 'bg-surface-selected text-foreground shadow-sm' : 'text-muted-foreground hover:text-foreground'}`}
          type="button"
          onClick={() => setOptions(prev => ({...prev, passphraseMode: false}))}>
          Password
        </button>
        <button
          className={`flex-1 rounded py-1 text-center font-medium ${options.passphraseMode ? 'bg-surface-selected text-foreground shadow-sm' : 'text-muted-foreground hover:text-foreground'}`}
          type="button"
          onClick={() => setOptions(prev => ({...prev, passphraseMode: true}))}>
          Passphrase
        </button>
      </div>

      {!options.passphraseMode ? (
        <div className="grid gap-2.5 pt-1">
          <div className="grid gap-1.5">
            <div className="flex items-center justify-between text-[11px] text-foreground">
              <span>Length</span>
              <span className="font-mono font-semibold tabular-nums text-primary">{options.length}</span>
            </div>
            <Slider
              aria-label="Password length"
              className="py-1"
              max={64}
              min={8}
              step={1}
              value={[options.length ?? 16]}
              onValueChange={([val]) => setOptions(prev => ({...prev, length: val ?? 16}))}
            />
          </div>

          <div className="grid grid-cols-2 gap-2 text-[11px] text-foreground">
            <label className="flex items-center gap-2 cursor-pointer">
              <Switch
                checked={options.useUppercase}
                onCheckedChange={checked => setOptions(prev => ({...prev, useUppercase: checked}))}
              />
              <span>Uppercase (A-Z)</span>
            </label>
            <label className="flex items-center gap-2 cursor-pointer">
              <Switch
                checked={options.useLowercase}
                onCheckedChange={checked => setOptions(prev => ({...prev, useLowercase: checked}))}
              />
              <span>Lowercase (a-z)</span>
            </label>
            <label className="flex items-center gap-2 cursor-pointer">
              <Switch
                checked={options.useDigits}
                onCheckedChange={checked => setOptions(prev => ({...prev, useDigits: checked}))}
              />
              <span>Numbers (0-9)</span>
            </label>
            <label className="flex items-center gap-2 cursor-pointer">
              <Switch
                checked={options.useSymbols}
                onCheckedChange={checked => setOptions(prev => ({...prev, useSymbols: checked}))}
              />
              <span>Symbols (!@#$)</span>
            </label>
            <label className="col-span-2 flex items-center gap-2 cursor-pointer">
              <Switch
                checked={options.avoidAmbiguous}
                onCheckedChange={checked => setOptions(prev => ({...prev, avoidAmbiguous: checked}))}
              />
              <span>Avoid ambiguous (1, l, I, 0, O)</span>
            </label>
          </div>
        </div>
      ) : (
        <div className="grid gap-2.5 pt-1">
          <div className="grid gap-1.5">
            <div className="flex items-center justify-between text-[11px] text-foreground">
              <span>Word count</span>
              <span className="font-mono font-semibold tabular-nums text-primary">{options.wordCount}</span>
            </div>
            <Slider
              aria-label="Passphrase word count"
              className="py-1"
              max={10}
              min={3}
              step={1}
              value={[options.wordCount ?? 4]}
              onValueChange={([val]) => setOptions(prev => ({...prev, wordCount: val ?? 4}))}
            />
          </div>
          <div className="grid grid-cols-2 gap-2 text-[11px] text-foreground">
            <div className="flex items-center gap-2">
              <span>Separator:</span>
              <Input
                aria-label="Passphrase separator"
                className="h-6 w-12 text-center text-xs"
                maxLength={2}
                value={options.separator}
                onChange={e => setOptions(prev => ({...prev, separator: e.target.value}))}
              />
            </div>
            <label className="flex items-center gap-2 cursor-pointer">
              <Switch
                checked={options.capitalize}
                onCheckedChange={checked => setOptions(prev => ({...prev, capitalize: checked}))}
              />
              <span>Capitalize</span>
            </label>
            <label className="col-span-2 flex items-center gap-2 cursor-pointer">
              <Switch
                checked={options.includeNumber}
                onCheckedChange={checked => setOptions(prev => ({...prev, includeNumber: checked}))}
              />
              <span>Include number</span>
            </label>
          </div>
        </div>
      )}

      {onUsePassword ? (
        <Button
          className="mt-1 h-8 w-full gap-1.5 bg-primary text-xs font-semibold text-primary-foreground hover:bg-primary/90"
          type="button"
          onClick={() => onUsePassword(generatedPassword)}>
          <Check className="size-3.5" />
          Use this password
        </Button>
      ) : null}
    </div>
  );
}

export type StandalonePasswordGeneratorDialogProps = {
  readonly onClose: () => void;
  readonly store: MahoSettingsStore;
};

export function StandalonePasswordGeneratorDialog({onClose, store}: StandalonePasswordGeneratorDialogProps) {
  return (
    <Dialog open onOpenChange={open => { if (!open) onClose(); }}>
      <DialogContent className="max-w-md border-border glass-strong p-6 text-foreground">
        <DialogHeader>
          <DialogTitle className="flex items-center gap-2 text-lg">
            <KeyRound className="size-5 text-primary" />
            Password Generator
          </DialogTitle>
          <DialogDescription className="text-muted-foreground">
            Generate strong, cryptographically random passwords or passphrases.
          </DialogDescription>
        </DialogHeader>
        <div className="py-2">
          <PasswordGenerator store={store} />
        </div>
        <DialogFooter>
          <Button type="button" variant="outline" onClick={onClose}>Close</Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}

type AddSavedPasswordDrawerProps = {
  readonly initialKind?: VaultItemKind;
  readonly onAdd: (input: AddSavedPasswordInput) => Promise<string | null>;
  readonly onAddSecureNote?: (input: AddSecureNoteInput) => Promise<string | null>;
  readonly onClose: () => void;
  readonly store?: MahoSettingsStore;
};

export function AddSavedPasswordDrawer(
    {initialKind = VaultItemKind.kLogin, onAdd, onAddSecureNote, onClose, store}: AddSavedPasswordDrawerProps) {
  const mountedRef = useRef(true);
  const passwordRef = useRef<HTMLInputElement | null>(null);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
  const [itemKind, setItemKind] = useState<VaultItemKind>(initialKind);
  const [title, setTitle] = useState('');
  const [origins, setOrigins] = useState<string[]>(['']);
  const [username, setUsername] = useState('');
  const [notes, setNotes] = useState('');
  const [totpSecret, setTotpSecret] = useState('');
  const [showGenerator, setShowGenerator] = useState(false);
  const [submitting, setSubmitting] = useState(false);

  const clearSecretInput = () => {
    const input = passwordRef.current;
    if (input) {
      input.value = '';
    }
  };

  const closeDrawer = () => {
    clearSecretInput();
    setErrorMessage(null);
    setTitle('');
    setOrigins(['']);
    setUsername('');
    setNotes('');
    setTotpSecret('');
    setShowGenerator(false);
    onClose();
  };

  useEffect(() => {
    return () => {
      mountedRef.current = false;
      clearSecretInput();
    };
  }, []);

  const handleOriginChange = (index: number, value: string) => {
    setOrigins(prev => {
      const next = [...prev];
      next[index] = value;
      return next;
    });
  };

  const handleAddOrigin = () => {
    setOrigins(prev => [...prev, '']);
  };

  const handleRemoveOrigin = (index: number) => {
    setOrigins(prev => prev.filter((_, i) => i !== index));
  };

  const handleUseGeneratedPassword = (pw: string) => {
    if (passwordRef.current) {
      passwordRef.current.value = pw;
      passwordRef.current.type = 'text';
      window.setTimeout(() => {
        if (passwordRef.current) passwordRef.current.type = 'password';
      }, 3000);
    }
    setShowGenerator(false);
  };

  const saveItem = async () => {
    if (submitting) {
      return;
    }
    if (itemKind === VaultItemKind.kSecureItem) {
      if (!title.trim()) {
        setErrorMessage('Enter a title for the secure note.');
        return;
      }
      setSubmitting(true);
      setErrorMessage(null);
      const message = onAddSecureNote ? await onAddSecureNote({title: title.trim(), notes}) : 'Secure notes not supported';
      if (!mountedRef.current) return;
      setSubmitting(false);
      if (message) {
        setErrorMessage(message);
        return;
      }
      closeDrawer();
      return;
    }

    const primaryOrigin = origins[0] ?? '';
    const secretInput = passwordRef.current;
    const password = secretInput ? secretInput.value : '';
    clearSecretInput();
    setSubmitting(true);
    setErrorMessage(null);
    const message = await onAdd({
      title: title.trim() || undefined,
      origin: primaryOrigin,
      origins: origins.map(o => o.trim()).filter(Boolean),
      password,
      username,
      notes: notes.trim() || undefined,
      totpSecret: totpSecret.trim() || undefined,
    });
    if (!mountedRef.current) {
      return;
    }
    setSubmitting(false);
    if (message) {
      setErrorMessage(message);
      return;
    }
    closeDrawer();
  };

  return (
    <Dialog open onOpenChange={open => {
      if (!open) {
        closeDrawer();
      }
    }}>
      <DialogContent className={DRAWER_CONTENT_CLASS}>
        <DialogHeader>
          <DialogTitle>Add to Vault</DialogTitle>
          <DialogDescription>
            Store encrypted credentials or notes in your local Maho Vault. Secrets are never exposed to the page.
          </DialogDescription>
        </DialogHeader>

        <div className="flex rounded-md border border-border bg-background/40 p-0.5 text-xs">
          <button
            className={`flex flex-1 items-center justify-center gap-1.5 rounded py-1.5 font-medium transition-colors ${itemKind === VaultItemKind.kLogin ? 'bg-surface-selected text-foreground shadow-sm' : 'text-muted-foreground hover:text-foreground'}`}
            aria-pressed={itemKind === VaultItemKind.kLogin}
            data-kind="login"
            type="button"
            onClick={() => setItemKind(VaultItemKind.kLogin)}>
            <Key className="size-3.5" />
            Login
          </button>
          <button
            className={`flex flex-1 items-center justify-center gap-1.5 rounded py-1.5 font-medium transition-colors ${itemKind === VaultItemKind.kSecureItem ? 'bg-surface-selected text-foreground shadow-sm' : 'text-muted-foreground hover:text-foreground'}`}
            aria-pressed={itemKind === VaultItemKind.kSecureItem}
            data-kind="secure-note"
            type="button"
            onClick={() => setItemKind(VaultItemKind.kSecureItem)}>
            <FileText className="size-3.5" />
            Secure Note
          </button>
        </div>

        <div className={FORM_CLASS}>
          {itemKind === VaultItemKind.kSecureItem ? (
            <>
              <div className={FIELD_GROUP_CLASS}>
                <label className={FIELD_LABEL_CLASS} htmlFor="secure-note-title">Title</label>
                <Input
                  aria-label="Secure note title"
                  id="secure-note-title"
                  placeholder="e.g. WiFi Passwords, Passport info"
                  value={title}
                  onChange={event => setTitle(event.currentTarget.value)}
                />
              </div>
              <div className={FIELD_GROUP_CLASS}>
                <label className={FIELD_LABEL_CLASS} htmlFor="secure-note-notes">Notes</label>
                <Textarea
                  aria-label="Secure note content"
                  className="min-h-32 text-xs"
                  id="secure-note-notes"
                  placeholder="Secret notes, recovery codes, or sensitive details…"
                  value={notes}
                  onChange={event => setNotes(event.currentTarget.value)}
                />
              </div>
            </>
          ) : (
            <>
              <div className={FIELD_GROUP_CLASS}>
                <label className={FIELD_LABEL_CLASS} htmlFor="saved-password-title">Title (optional)</label>
                <Input
                  aria-label="Saved password title"
                  id="saved-password-title"
                  placeholder="e.g. Personal Google, Work GitHub"
                  value={title}
                  onChange={event => setTitle(event.currentTarget.value)}
                />
              </div>

              <div className={FIELD_GROUP_CLASS}>
                <label className={FIELD_LABEL_CLASS} htmlFor="saved-password-origin">Websites</label>
                {origins.map((origin, index) => (
                  <div key={index} className="flex items-center gap-1.5">
                    <Input
                      aria-label={index === 0 ? 'Saved password origin' : `Saved password secondary origin ${index}`}
                      autoComplete="url"
                      id={index === 0 ? 'saved-password-origin' : `saved-password-origin-${index}`}
                      placeholder="https://example.com"
                      value={origin}
                      onChange={event => handleOriginChange(index, event.currentTarget.value)}
                    />
                    {index > 0 ? (
                      <Button
                        aria-label={`Remove website ${index}`}
                        className="size-9 shrink-0 text-muted-foreground hover:text-destructive"
                        size="icon"
                        type="button"
                        variant="ghost"
                        onClick={() => handleRemoveOrigin(index)}>
                        <X className="size-4" />
                      </Button>
                    ) : null}
                  </div>
                ))}
                <Button
                  className="h-7 w-fit gap-1 text-[11px] text-primary hover:text-foreground"
                  size="sm"
                  type="button"
                  variant="ghost"
                  onClick={handleAddOrigin}>
                  <Plus className="size-3" />
                  Add another URL
                </Button>
              </div>

              <div className={FIELD_GROUP_CLASS}>
                <label className={FIELD_LABEL_CLASS} htmlFor="saved-password-username">Username</label>
                <Input
                  aria-label="Saved password username"
                  autoComplete="username"
                  id="saved-password-username"
                  placeholder="name@example.com"
                  value={username}
                  onChange={event => setUsername(event.currentTarget.value)}
                />
              </div>

              <div className={FIELD_GROUP_CLASS}>
                <div className="flex items-center justify-between">
                  <label className={FIELD_LABEL_CLASS} htmlFor="saved-password-value">Password</label>
                  <Button
                    className="h-6 gap-1 text-[11px] text-primary hover:text-foreground"
                    size="sm"
                    type="button"
                    variant="ghost"
                    onClick={() => setShowGenerator(prev => !prev)}>
                    <Sparkles className="size-3" />
                    {showGenerator ? 'Hide generator' : 'Generate'}
                  </Button>
                </div>
                <Input
                  aria-label="Saved password value"
                  autoComplete="new-password"
                  id="saved-password-value"
                  placeholder="Password"
                  ref={passwordRef}
                  type="password"
                />
                {showGenerator ? (
                  <PasswordGenerator onUsePassword={handleUseGeneratedPassword} store={store} />
                ) : null}
              </div>

              <div className={FIELD_GROUP_CLASS}>
                <label className={FIELD_LABEL_CLASS} htmlFor="saved-password-totp">Authenticator Key (TOTP secret)</label>
                <Input
                  aria-label="Saved password TOTP secret"
                  id="saved-password-totp"
                  placeholder="Base32 secret or otpauth:// URI"
                  value={totpSecret}
                  onChange={event => setTotpSecret(event.currentTarget.value)}
                />
                <p className={FIELD_HELP_CLASS}>Maho generates 6-digit verification codes locally from this key.</p>
              </div>

              <div className={FIELD_GROUP_CLASS}>
                <label className={FIELD_LABEL_CLASS} htmlFor="saved-password-notes">Notes</label>
                <Textarea
                  aria-label="Saved password notes"
                  className="min-h-20 text-xs"
                  id="saved-password-notes"
                  placeholder="Additional notes or security questions…"
                  value={notes}
                  onChange={event => setNotes(event.currentTarget.value)}
                />
              </div>
            </>
          )}

          <p
            aria-live="assertive"
            className={errorMessage ? ERROR_CLASS : 'sr-only'}
            role="alert">
            {errorMessage ?? ''}
          </p>
        </div>

        <DialogFooter className="sticky bottom-0 bg-background/80 pt-2 backdrop-blur-sm">
          <Button type="button" variant="outline" onClick={closeDrawer}>Cancel</Button>
          <Button type="button" disabled={submitting} onClick={() => void saveItem()}>
            {submitting ? 'Saving…' : itemKind === VaultItemKind.kSecureItem ? 'Save note' : 'Save password'}
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}

type EditSavedPasswordDrawerProps = {
  readonly item: VaultItem;
  readonly onClose: () => void;
  readonly onSave: (item: VaultItem, input: EditSavedPasswordInput) => Promise<string | null>;
  readonly onSaveSecureNote?: (item: VaultItem, input: EditSecureNoteInput) => Promise<string | null>;
  readonly store?: MahoSettingsStore;
};

export function EditSavedPasswordDrawer(
    {item, onClose, onSave, onSaveSecureNote, store}: EditSavedPasswordDrawerProps) {
  const mountedRef = useRef(true);
  const passwordRef = useRef<HTMLInputElement | null>(null);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
  const [title, setTitle] = useState(item.title || '');
  const [origins, setOrigins] = useState<string[]>(item.origins.length > 0 ? [...item.origins] : [getPrimaryOrigin(item)]);
  // Never prefill from usernameHint: it is a masked display value, and saving
  // it back would overwrite the stored username. Load the real value instead.
  const [username, setUsername] = useState('');
  const [usernameLoaded, setUsernameLoaded] = useState(false);
  const [notes, setNotes] = useState('');
  const [totpSecret, setTotpSecret] = useState('');
  const [showGenerator, setShowGenerator] = useState(false);
  const [submitting, setSubmitting] = useState(false);

  const clearSecretInput = () => {
    const input = passwordRef.current;
    if (input) {
      input.value = '';
    }
  };

  useEffect(() => {
    return () => {
      mountedRef.current = false;
      clearSecretInput();
    };
  }, []);

  useEffect(() => {
    clearSecretInput();
    setErrorMessage(null);
    setTitle(item.title || '');
    setOrigins(item.origins.length > 0 ? [...item.origins] : [getPrimaryOrigin(item)]);
    setUsername('');
    setUsernameLoaded(false);
    setShowGenerator(false);

    const handler = store?.getHandler();
    if (handler && typeof handler.getVaultItemNotes === 'function') {
      void handler.getVaultItemNotes(item.id).then(res => {
        if (!mountedRef.current) return;
        if (!res.success) {
          setErrorMessage('Could not load this login. Close the editor and try again.');
          return;
        }
        if (res.notes) {
          setNotes(res.notes);
        }
        setUsername(res.username ?? '');
        setUsernameLoaded(true);
      }).catch(() => {
        if (mountedRef.current) {
          setErrorMessage('Could not load this login. Close the editor and try again.');
        }
      });
    }
  }, [item, store]);

  const closeDrawer = () => {
    clearSecretInput();
    setErrorMessage(null);
    onClose();
  };

  const handleOriginChange = (index: number, value: string) => {
    setOrigins(prev => {
      const next = [...prev];
      next[index] = value;
      return next;
    });
  };

  const handleAddOrigin = () => {
    setOrigins(prev => [...prev, '']);
  };

  const handleRemoveOrigin = (index: number) => {
    setOrigins(prev => prev.filter((_, i) => i !== index));
  };

  const handleUseGeneratedPassword = (pw: string) => {
    if (passwordRef.current) {
      passwordRef.current.value = pw;
      passwordRef.current.type = 'text';
      window.setTimeout(() => {
        if (passwordRef.current) passwordRef.current.type = 'password';
      }, 3000);
    }
    setShowGenerator(false);
  };

  const saveChanges = async () => {
    if (submitting) {
      return;
    }
    if (item.itemKind === VaultItemKind.kSecureItem) {
      if (!title.trim()) {
        setErrorMessage('Enter a title for the secure note.');
        return;
      }
      setSubmitting(true);
      setErrorMessage(null);
      const message = onSaveSecureNote ? await onSaveSecureNote(item, {title: title.trim(), notes}) : 'Secure note updates unavailable';
      if (!mountedRef.current) return;
      setSubmitting(false);
      if (message) {
        setErrorMessage(message);
        return;
      }
      closeDrawer();
      return;
    }

    const primaryOrigin = origins[0] ?? '';
    const secretInput = passwordRef.current;
    const password = secretInput?.value || null;
    clearSecretInput();
    setSubmitting(true);
    setErrorMessage(null);
    const message = await onSave(item, {
      title: title.trim() || undefined,
      origin: primaryOrigin,
      origins: origins.map(o => o.trim()).filter(Boolean),
      password,
      username: username.trim(),
      notes: notes.trim() || undefined,
      totpSecret: totpSecret.trim() || undefined,
    });
    if (!mountedRef.current) {
      return;
    }
    setSubmitting(false);
    if (message) {
      setErrorMessage(message);
      return;
    }
    closeDrawer();
  };

  return (
    <Dialog open onOpenChange={open => {
      if (!open) {
        closeDrawer();
      }
    }}>
      <DialogContent className={DRAWER_CONTENT_CLASS}>
        <DialogHeader>
          <DialogTitle>Edit {item.itemKind === VaultItemKind.kSecureItem ? 'secure note' : 'saved password'}</DialogTitle>
          <DialogDescription>
            Update {getDisplayName(item)}. The existing password remains hidden; enter a new password only when replacing it.
          </DialogDescription>
        </DialogHeader>

        <div className={FORM_CLASS}>
          {item.itemKind === VaultItemKind.kSecureItem ? (
            <>
              <div className={FIELD_GROUP_CLASS}>
                <label className={FIELD_LABEL_CLASS} htmlFor="edit-secure-note-title">Title</label>
                <Input
                  aria-label="Edit secure note title"
                  id="edit-secure-note-title"
                  value={title}
                  onChange={event => setTitle(event.currentTarget.value)}
                />
              </div>
              <div className={FIELD_GROUP_CLASS}>
                <label className={FIELD_LABEL_CLASS} htmlFor="edit-secure-note-notes">Notes</label>
                <Textarea
                  aria-label="Edit secure note notes"
                  className="min-h-36 text-xs"
                  id="edit-secure-note-notes"
                  value={notes}
                  onChange={event => setNotes(event.currentTarget.value)}
                />
              </div>
            </>
          ) : (
            <>
              <div className={FIELD_GROUP_CLASS}>
                <label className={FIELD_LABEL_CLASS} htmlFor="edit-saved-password-title">Title</label>
                <Input
                  aria-label="Edit saved password title"
                  id="edit-saved-password-title"
                  placeholder="e.g. Personal Google"
                  value={title}
                  onChange={event => setTitle(event.currentTarget.value)}
                />
              </div>

              <div className={FIELD_GROUP_CLASS}>
                <label className={FIELD_LABEL_CLASS} htmlFor="edit-saved-password-origin">Websites</label>
                {origins.map((origin, index) => (
                  <div key={index} className="flex items-center gap-1.5">
                    <Input
                      aria-label={index === 0 ? 'Edit saved password origin' : `Edit saved password secondary origin ${index}`}
                      autoComplete="url"
                      id={index === 0 ? 'edit-saved-password-origin' : `edit-saved-password-origin-${index}`}
                      placeholder="https://example.com"
                      value={origin}
                      onChange={event => handleOriginChange(index, event.currentTarget.value)}
                    />
                    {index > 0 ? (
                      <Button
                        aria-label={`Remove website ${index}`}
                        className="size-9 shrink-0 text-muted-foreground hover:text-destructive"
                        size="icon"
                        type="button"
                        variant="ghost"
                        onClick={() => handleRemoveOrigin(index)}>
                        <X className="size-4" />
                      </Button>
                    ) : null}
                  </div>
                ))}
                <Button
                  className="h-7 w-fit gap-1 text-[11px] text-primary hover:text-foreground"
                  size="sm"
                  type="button"
                  variant="ghost"
                  onClick={handleAddOrigin}>
                  <Plus className="size-3" />
                  Add another URL
                </Button>
              </div>

              <div className={FIELD_GROUP_CLASS}>
                <label className={FIELD_LABEL_CLASS} htmlFor="edit-saved-password-username">Username</label>
                <Input
                  aria-label="Edit saved password username"
                  autoComplete="username"
                  id="edit-saved-password-username"
                  placeholder="name@example.com"
                  value={username}
                  onChange={event => setUsername(event.currentTarget.value)}
                />
              </div>

              <div className={FIELD_GROUP_CLASS}>
                <div className="flex items-center justify-between">
                  <label className={FIELD_LABEL_CLASS} htmlFor="edit-saved-password-value">New password (optional)</label>
                  <Button
                    className="h-6 gap-1 text-[11px] text-primary hover:text-foreground"
                    size="sm"
                    type="button"
                    variant="ghost"
                    onClick={() => setShowGenerator(prev => !prev)}>
                    <Sparkles className="size-3" />
                    {showGenerator ? 'Hide generator' : 'Generate'}
                  </Button>
                </div>
                <Input
                  aria-label="Replace saved password value"
                  autoComplete="new-password"
                  id="edit-saved-password-value"
                  placeholder="Leave blank to keep current password"
                  ref={passwordRef}
                  type="password"
                />
                {showGenerator ? (
                  <PasswordGenerator onUsePassword={handleUseGeneratedPassword} store={store} />
                ) : null}
                <p className={FIELD_HELP_CLASS}>Leave this blank to keep current password. Stored passwords never appear in this form.</p>
              </div>

              <div className={FIELD_GROUP_CLASS}>
                <label className={FIELD_LABEL_CLASS} htmlFor="edit-saved-password-totp">Authenticator Key (TOTP)</label>
                <Input
                  aria-label="Edit saved password TOTP secret"
                  id="edit-saved-password-totp"
                  placeholder={item.hasTotp ? '•••••••• (Enter new key to replace)' : 'Base32 secret or otpauth:// URI'}
                  value={totpSecret}
                  onChange={event => setTotpSecret(event.currentTarget.value)}
                />
              </div>

              <div className={FIELD_GROUP_CLASS}>
                <label className={FIELD_LABEL_CLASS} htmlFor="edit-saved-password-notes">Notes</label>
                <Textarea
                  aria-label="Edit saved password notes"
                  className="min-h-20 text-xs"
                  id="edit-saved-password-notes"
                  placeholder="Additional notes or security questions…"
                  value={notes}
                  onChange={event => setNotes(event.currentTarget.value)}
                />
              </div>
            </>
          )}

          <p
            aria-live="assertive"
            className={errorMessage ? ERROR_CLASS : 'sr-only'}
            role="alert">
            {errorMessage ?? ''}
          </p>
        </div>

        <DialogFooter className="sticky bottom-0 bg-background/80 pt-2 backdrop-blur-sm">
          <Button type="button" variant="outline" onClick={closeDrawer}>Cancel</Button>
          <Button type="button" disabled={submitting || !usernameLoaded} onClick={() => void saveChanges()}>
            {submitting ? 'Saving…' : 'Save changes'}
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}
