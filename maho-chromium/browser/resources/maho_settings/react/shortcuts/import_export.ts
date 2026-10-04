import {toast} from 'sonner';
import {PageHandlerRemote} from '../../mojo.js';

const SHORTCUTS_EXPORT_FILENAME = 'maho-shortcuts.json';

function isKeyCombo(value: unknown): boolean {
  return value !== null &&
      typeof value === 'object' &&
      !Array.isArray(value) &&
      'key' in value &&
      typeof value.key === 'string' &&
      'modifiers' in value &&
      Array.isArray(value.modifiers) &&
      value.modifiers.every(modifier => typeof modifier === 'string');
}

function isShortcutExportEntry(value: unknown): boolean {
  return value !== null &&
      typeof value === 'object' &&
      !Array.isArray(value) &&
      'action' in value &&
      typeof value.action === 'string' &&
      value.action.length > 0 &&
      'label' in value &&
      typeof value.label === 'string' &&
      'category' in value &&
      typeof value.category === 'string' &&
      value.category.length > 0 &&
      'keyCombo' in value &&
      isKeyCombo(value.keyCombo) &&
      'defaultKeyCombo' in value &&
      isKeyCombo(value.defaultKeyCombo) &&
      'isCustom' in value &&
      typeof value.isCustom === 'boolean' &&
      'enabled' in value &&
      typeof value.enabled === 'boolean' &&
      'updatedAt' in value &&
      typeof value.updatedAt === 'number' &&
      Number.isFinite(value.updatedAt);
}

function validateShortcutExportJson(jsonText: string): string | null {
  try {
    const parsed: unknown = JSON.parse(jsonText);
    if (parsed === null || typeof parsed !== 'object' || Array.isArray(parsed)) {
      return 'Shortcut export must contain a JSON object.';
    }
    if (!('version' in parsed) || parsed.version !== 1) {
      return 'Shortcut export has an unsupported or missing version.';
    }
    if (!('platform' in parsed) ||
        typeof parsed.platform !== 'string' ||
        !['macos', 'windows', 'linux'].includes(parsed.platform)) {
      return 'Shortcut export has an invalid platform.';
    }
    if (!('exportedAt' in parsed) ||
        typeof parsed.exportedAt !== 'string' ||
        !/^\d{4}-\d{2}-\d{2}T/.test(parsed.exportedAt) ||
        Number.isNaN(Date.parse(parsed.exportedAt))) {
      return 'Shortcut export has an invalid exportedAt timestamp.';
    }
    if (!('shortcuts' in parsed) ||
        !Array.isArray(parsed.shortcuts) ||
        parsed.shortcuts.length === 0) {
      return 'Shortcut export must contain at least one shortcut.';
    }
    if (!parsed.shortcuts.every(isShortcutExportEntry)) {
      return 'Shortcut export contains an invalid shortcut entry.';
    }
    return null;
  } catch (error) {
    if (error instanceof SyntaxError) {
      return 'Shortcut export is not valid JSON.';
    }
    throw error;
  }
}

function validateShortcutImportJson(jsonText: string): string | null {
  try {
    const parsed: unknown = JSON.parse(jsonText);
    if (parsed === null || (typeof parsed !== 'object' && !Array.isArray(parsed))) {
      return 'Shortcut files must contain a JSON object or array.';
    }
    return null;
  } catch (error) {
    if (error instanceof SyntaxError) {
      return 'Selected file is not valid JSON.';
    }
    throw error;
  }
}

function readTextFile(file: File): Promise<string> {
  return new Promise((resolve, reject) => {
    const reader = new FileReader();
    reader.onload = () => resolve(String(reader.result ?? ''));
    reader.onerror = () => reject(reader.error ?? new Error(`Failed to read ${file.name}.`));
    reader.readAsText(file);
  });
}

export async function exportShortcutsToFile(handler: PageHandlerRemote): Promise<void> {
  try {
    const {jsonData} = await handler.exportShortcuts();
    const validationError = validateShortcutExportJson(jsonData);
    if (validationError) {
      throw new Error(validationError);
    }

    const blob = new Blob([jsonData], {type: 'application/json'});
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = SHORTCUTS_EXPORT_FILENAME;
    link.click();
    URL.revokeObjectURL(url);

    toast.success('Shortcuts exported', {description: SHORTCUTS_EXPORT_FILENAME});
  } catch (error) {
    const message = error instanceof Error ? error.message : 'Failed to export shortcuts.';
    console.error(error);
    toast.error(message, {
      description: 'Unable to create the shortcuts export file.',
    });
  }
}

export async function importShortcutsFromFile(
    handler: PageHandlerRemote,
    onSuccess: () => void,
    onError: (msg: string) => void): Promise<void> {
  await new Promise<void>(resolve => {
    const input = document.createElement('input');
    input.type = 'file';
    input.accept = '.json,application/json';

    let settled = false;
    const finish = () => {
      if (settled) {
        return;
      }
      settled = true;
      resolve();
    };

    const handleWindowFocus = () => {
      window.setTimeout(() => {
        if (!input.files?.length) {
          finish();
        }
      }, 0);
    };

    window.addEventListener('focus', handleWindowFocus, {once: true});

    input.onchange = async event => {
      try {
        const file = (event.target as HTMLInputElement).files?.[0];
        if (!file) {
          finish();
          return;
        }

        const jsonText = await readTextFile(file);
        const validationError = validateShortcutImportJson(jsonText);
        if (validationError) {
          toast.error(validationError, {description: file.name});
          onError(validationError);
          finish();
          return;
        }

        const {success} = await handler.importShortcuts(jsonText);
        if (!success) {
          const message = 'Failed to import shortcuts. Please verify file format.';
          toast.error(message, {description: file.name});
          onError(message);
          finish();
          return;
        }

        await Promise.resolve(onSuccess());
        toast.success('Shortcuts imported', {description: file.name});
      } catch (error) {
        const message = error instanceof Error ? error.message : 'Failed to import shortcuts.';
        console.error(error);
        toast.error(message, {description: 'Import was cancelled or unreadable.'});
        onError(message);
      } finally {
        finish();
      }
    };

    input.click();
  });
}
