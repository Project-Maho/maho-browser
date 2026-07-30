// Copyright 2026 Maho Browser. All rights reserved.

import { Listener, notifyListeners } from '../../maho_common/react/store_utils.js';
import * as api from './api/index.js';
import type * as Types from './mail_types';

class MailStore {
  private listeners = new Set<Listener>();

  // State
  private accounts: Types.AccountSummary[] = [];
  private folders: Record<string, Types.Folder[]> = {}; // accountId -> folders
  private selection: Types.MailboxSelection = null;
  private emails: Types.EmailSummary[] = [];
  private selectedEmail: Types.EmailDetail | null = null;
  private loading = false;
  private error: string | null = null;

  subscribe(listener: Listener): () => void {
    this.listeners.add(listener);
    return () => {
      this.listeners.delete(listener);
    };
  }

  private notify() {
    notifyListeners(this.listeners);
  }

  // Getters
  getAccounts() { return this.accounts; }
  getFolders(accountId: string) { return this.folders[accountId] || []; }
  getSelection() { return this.selection; }
  getEmails() { return this.emails; }
  getSelectedEmail() { return this.selectedEmail; }
  isLoading() { return this.loading; }
  getError() { return this.error; }

  // Actions
  async loadAccounts() {
    this.loading = true;
    this.notify();
    try {
      this.accounts = await api.listAccounts();
      this.error = null;
    } catch (e) {
      this.error = e instanceof Error ? e.message : String(e);
    } finally {
      this.loading = false;
      this.notify();
    }
  }

  async loadFolders(accountId: string) {
    try {
      this.folders[accountId] = await api.listFolders(accountId);
      this.notify();
    } catch (e) {
      console.error('Failed to load folders:', e);
    }
  }

  setSelection(selection: Types.MailboxSelection) {
    this.selection = selection;
    this.notify();
  }

  async loadEmails(accountId: string, folderId: string, limit = 50, offset = 0) {
    this.loading = true;
    this.notify();
    try {
      this.emails = await api.listEmails(accountId, folderId, limit, offset);
      this.error = null;
    } catch (e) {
      this.error = e instanceof Error ? e.message : String(e);
    } finally {
      this.loading = false;
      this.notify();
    }
  }

  async loadEmail(emailId: string) {
    try {
      this.selectedEmail = await api.getEmail(emailId);
      this.notify();
    } catch (e) {
      console.error('Failed to load email detail:', e);
    }
  }
}

export const mailStore = new MailStore();
export default mailStore;
