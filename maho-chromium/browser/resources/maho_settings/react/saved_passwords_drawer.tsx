// Copyright 2026 Maho Browser. All rights reserved.

import React, {useEffect, useRef, useState} from 'react';

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
import {getDisplayName, getPrimaryOrigin} from './saved_passwords_model.js';
import type {
  AddSavedPasswordInput,
  EditSavedPasswordInput,
} from './saved_passwords_model.js';
import type {VaultItem} from '../mojo.js';

const DRAWER_CONTENT_CLASS =
    'left-auto right-0 top-0 h-full max-w-md translate-x-0 translate-y-0 rounded-none border-y-0 border-r-0 glass-strong sm:rounded-none data-[state=closed]:slide-out-to-right data-[state=open]:slide-in-from-right';
const FIELD_GROUP_CLASS = 'grid gap-2';
const FIELD_LABEL_CLASS = 'text-xs font-medium text-foreground';
const FIELD_HELP_CLASS = 'text-xs leading-5 text-muted-foreground';
const FORM_CLASS = 'grid gap-4';
const ERROR_CLASS = 'text-sm leading-6 text-destructive';

type AddSavedPasswordDrawerProps = {
  readonly onAdd: (input: AddSavedPasswordInput) => Promise<string | null>;
  readonly onClose: () => void;
};

export function AddSavedPasswordDrawer({onAdd, onClose}: AddSavedPasswordDrawerProps) {
  const mountedRef = useRef(true);
  const passwordRef = useRef<HTMLInputElement | null>(null);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
  const [origin, setOrigin] = useState('');
  const [submitting, setSubmitting] = useState(false);
  const [username, setUsername] = useState('');

  const clearSecretInput = () => {
    const input = passwordRef.current;
    if (input) {
      input.value = '';
    }
  };

  const closeDrawer = () => {
    clearSecretInput();
    setErrorMessage(null);
    setOrigin('');
    setUsername('');
    onClose();
  };

  useEffect(() => {
    return () => {
      mountedRef.current = false;
      clearSecretInput();
    };
  }, []);

  const savePassword = async () => {
    if (submitting) {
      return;
    }
    const secretInput = passwordRef.current;
    const password = secretInput ? secretInput.value : '';
    clearSecretInput();
    setSubmitting(true);
    setErrorMessage(null);
    const message = await onAdd({origin, password, username});
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
          <DialogTitle>Add saved password</DialogTitle>
          <DialogDescription>
            Store a native Vault login. The password is sent directly to the browser process and is never read back into this page.
          </DialogDescription>
        </DialogHeader>
        <div className={FORM_CLASS}>
          <div className={FIELD_GROUP_CLASS}>
            <label className={FIELD_LABEL_CLASS} htmlFor="saved-password-origin">Site</label>
            <Input
              aria-label="Saved password origin"
              autoComplete="url"
              id="saved-password-origin"
              placeholder="https://example.com"
              value={origin}
              onChange={event => setOrigin(event.currentTarget.value)}
            />
            <p className={FIELD_HELP_CLASS}>Enter a domain or URL. Maho normalizes it to the site origin before saving.</p>
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
            <label className={FIELD_LABEL_CLASS} htmlFor="saved-password-value">Password</label>
            <Input
              aria-label="Saved password value"
              autoComplete="new-password"
              id="saved-password-value"
              placeholder="Password"
              ref={passwordRef}
              type="password"
            />
            <p className={FIELD_HELP_CLASS}>The secret is cleared from this form as soon as the save request starts.</p>
          </div>
          <p
            aria-live="assertive"
            className={errorMessage ? ERROR_CLASS : 'sr-only'}
            role="alert">
            {errorMessage ?? ''}
          </p>
        </div>
        <DialogFooter>
          <Button type="button" variant="outline" onClick={closeDrawer}>Cancel</Button>
          <Button type="button" disabled={submitting} onClick={() => void savePassword()}>
            {submitting ? 'Saving…' : 'Save password'}
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
};

export function EditSavedPasswordDrawer(
    {item, onClose, onSave}: EditSavedPasswordDrawerProps) {
  const mountedRef = useRef(true);
  const passwordRef = useRef<HTMLInputElement | null>(null);
  const [errorMessage, setErrorMessage] = useState<string | null>(null);
  const [origin, setOrigin] = useState(getPrimaryOrigin(item));
  const [submitting, setSubmitting] = useState(false);
  const [username, setUsername] = useState(item.usernameHint);

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
    setOrigin(getPrimaryOrigin(item));
    setUsername(item.usernameHint);
  }, [item]);

  const closeDrawer = () => {
    clearSecretInput();
    setErrorMessage(null);
    setOrigin('');
    setUsername('');
    onClose();
  };

  const saveChanges = async () => {
    if (submitting) {
      return;
    }
    const secretInput = passwordRef.current;
    const password = secretInput?.value || null;
    clearSecretInput();
    setSubmitting(true);
    setErrorMessage(null);
    const message = await onSave(item, {origin, password, username});
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
          <DialogTitle>Edit saved password</DialogTitle>
          <DialogDescription>
            Update {getDisplayName(item)}. The existing password remains hidden; enter a new password only when you want to replace it.
          </DialogDescription>
        </DialogHeader>
        <div className={FORM_CLASS}>
          <div className={FIELD_GROUP_CLASS}>
            <label className={FIELD_LABEL_CLASS} htmlFor="edit-saved-password-origin">Site</label>
            <Input
              aria-label="Edit saved password origin"
              autoComplete="url"
              id="edit-saved-password-origin"
              placeholder="https://example.com"
              value={origin}
              onChange={event => setOrigin(event.currentTarget.value)}
            />
            <p className={FIELD_HELP_CLASS}>Maho stores the normalized site origin, not a page path.</p>
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
            <label className={FIELD_LABEL_CLASS} htmlFor="edit-saved-password-value">New password (optional)</label>
            <Input
              aria-label="Replace saved password value"
              autoComplete="new-password"
              id="edit-saved-password-value"
              placeholder="Leave blank to keep current password"
              ref={passwordRef}
              type="password"
            />
            <p className={FIELD_HELP_CLASS}>Leave this blank to keep the current secret unchanged. Any replacement is cleared from the form when saving starts.</p>
          </div>
          <p
            aria-live="assertive"
            className={errorMessage ? ERROR_CLASS : 'sr-only'}
            role="alert">
            {errorMessage ?? ''}
          </p>
        </div>
        <DialogFooter>
          <Button type="button" variant="outline" onClick={closeDrawer}>Cancel</Button>
          <Button type="button" disabled={submitting} onClick={() => void saveChanges()}>
            {submitting ? 'Saving…' : 'Save changes'}
          </Button>
        </DialogFooter>
      </DialogContent>
    </Dialog>
  );
}
