import {toast} from 'sonner';
import {PageHandlerRemote} from '../../mojo.js';

const SHORTCUTS_EXPORT_FILENAME = 'maho-shortcuts.json';

function resolveErrorMessage(error: unknown, fallback: string): string {
  return error instanceof Error ? error.message : fallback;
}

function validateShortcutJson(jsonText: string): string | null {
  try {
    const parsed = JSON.parse(jsonText) as unknown;
    if (parsed === null || (typeof parsed !== 'object' && !Array.isArray(parsed))) {
      return 'Shortcut files must contain a JSON object or array.';
    }
    return null;
  } catch {
    return 'Selected file is not valid JSON.';
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
    const validationError = validateShortcutJson(jsonData);
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
    console.error(error);
    toast.error(resolveErrorMessage(error, 'Failed to export shortcuts.'), {
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
        const validationError = validateShortcutJson(jsonText);
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
        const message = resolveErrorMessage(error, 'Failed to import shortcuts.');
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
