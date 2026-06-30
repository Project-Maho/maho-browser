import {describe, it, expect} from 'vitest';
import {
  createFileAttachment,
  getSubmitContextAttachments,
  hasCurrentPageAttachment,
  isFileAttachment,
  CURRENT_PAGE_ATTACHMENT,
  type ComposerAttachment,
} from '../../types.js';
import {ContextSourceKind} from '../../maho_ai.mojom-webui.js';

const ONE_BY_ONE_TRANSPARENT_PNG_DATA_URL =
    'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNkAAIAAAoAAv/lxKUAAAAASUVORK5CYII=';
const PNG_FILE_SIGNATURE = [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a];

function fakeFile(name: string, type: string, bytes: Uint8Array): File {
  const buffer = new ArrayBuffer(bytes.byteLength);
  new Uint8Array(buffer).set(bytes);
  return new File([buffer], name, {type});
}

describe('getSubmitContextAttachments — file attachment integration', () => {
  it('returns null when no eligible attachments', () => {
    expect(getSubmitContextAttachments([])).toBeNull();
  });

  it('includes file attachment with mimeType + bytes populated', async () => {
    const png = new Uint8Array([0x89, 0x50, 0x4e, 0x47]);
    const file = fakeFile('shot.png', 'image/png', png);
    const attachment = await createFileAttachment(file);

    const result = getSubmitContextAttachments([attachment]);
    expect(result).not.toBeNull();
    expect(result).toHaveLength(1);
    expect(result![0].kind).toBe(ContextSourceKind.kFile);
    expect(result![0].mimeType).toBe('image/png');
    expect(result![0].bytes).toEqual([0x89, 0x50, 0x4e, 0x47]);
  });

  it('preserves order when currentPage and a file are attached together', async () => {
    const png = new Uint8Array([0xff]);
    const file = fakeFile('a.png', 'image/png', png);
    const fileAttachment = await createFileAttachment(file);

    const attachments: ComposerAttachment[] = [
      {...CURRENT_PAGE_ATTACHMENT},
      fileAttachment,
    ];

    const result = getSubmitContextAttachments(attachments);
    expect(result).not.toBeNull();
    expect(result).toHaveLength(2);
    expect(result![0].kind).toBe(ContextSourceKind.kCurrentPage);
    expect(result![0].bytes).toEqual([]);
    expect(result![1].kind).toBe(ContextSourceKind.kFile);
    expect(result![1].mimeType).toBe('image/png');
    expect(result![1].bytes).toEqual([0xff]);
  });

  it('handles multiple file attachments', async () => {
    const a = await createFileAttachment(fakeFile('a.png', 'image/png', new Uint8Array([0x01])));
    const b = await createFileAttachment(fakeFile('b.jpg', 'image/jpeg', new Uint8Array([0x02])));

    const result = getSubmitContextAttachments([a, b]);
    expect(result).toHaveLength(2);
    expect(result![0].mimeType).toBe('image/png');
    expect(result![0].bytes).toEqual([0x01]);
    expect(result![1].mimeType).toBe('image/jpeg');
    expect(result![1].bytes).toEqual([0x02]);
  });
});

describe('createFileAttachment', () => {
  it('rejects SVG by MIME type', async () => {
    const svg = fakeFile('logo.svg', 'image/svg+xml', new Uint8Array([0x3c]));
    await expect(createFileAttachment(svg)).rejects.toThrow(/SVG/);
  });

  it('rejects SVG by file extension even when MIME is generic', async () => {
    const svg = fakeFile('logo.SVG', '', new Uint8Array([0x3c]));
    await expect(createFileAttachment(svg)).rejects.toThrow(/SVG/);
  });

  it('produces a stable shape for valid image', async () => {
    const png = fakeFile('hello.png', 'image/png', new Uint8Array([0x89]));
    const att = await createFileAttachment(png);

    expect(att.kind).toBe('file');
    expect(att.label).toBe('hello.png');
    expect(att.mimeType).toBe('image/png');
    expect(att.size).toBe(1);
    expect(att.dataUrl).toMatch(/^data:image\/png;base64,/);
    expect(isFileAttachment(att)).toBe(true);
  });
});

describe('dataUrlToByteArray edge cases (via getSubmitContextAttachments)', () => {
  it('returns empty bytes when dataUrl is undefined', () => {
    const noDataUrl: ComposerAttachment = {
      id: 'file:nodata',
      kind: 'file',
      label: 'nodata',
      mimeType: 'image/png',
    };
    const result = getSubmitContextAttachments([noDataUrl]);
    expect(result).toHaveLength(1);
    expect(result![0].bytes).toEqual([]);
  });

  it('returns empty bytes when dataUrl has no comma separator', () => {
    const malformed: ComposerAttachment = {
      id: 'file:nocomma',
      kind: 'file',
      label: 'nocomma',
      mimeType: 'image/png',
      dataUrl: 'data:image/png;base64iVBORw',
    };
    const result = getSubmitContextAttachments([malformed]);
    expect(result).toHaveLength(1);
    expect(result![0].bytes).toEqual([]);
  });

  it('decodes a real PNG data URL preserving the file signature', () => {
    const att: ComposerAttachment = {
      id: 'file:real',
      kind: 'file',
      label: 'real',
      mimeType: 'image/png',
      dataUrl: ONE_BY_ONE_TRANSPARENT_PNG_DATA_URL,
    };
    const result = getSubmitContextAttachments([att]);
    expect(result).toHaveLength(1);
    expect(result![0].bytes!.slice(0, PNG_FILE_SIGNATURE.length))
        .toEqual(PNG_FILE_SIGNATURE);
  });
});

describe('hasCurrentPageAttachment', () => {
  it('detects currentPage by id', () => {
    expect(hasCurrentPageAttachment([CURRENT_PAGE_ATTACHMENT])).toBe(true);
    expect(hasCurrentPageAttachment([])).toBe(false);
  });
});
