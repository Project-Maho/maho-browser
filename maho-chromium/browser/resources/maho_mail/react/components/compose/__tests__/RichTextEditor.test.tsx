import { render, screen, fireEvent, waitFor } from "@testing-library/react";
import { describe, it, expect, vi, beforeEach, type MockedFunction } from "vitest";
import { RichTextEditor } from "../RichTextEditor";

const toastMock = vi.fn();

const toggleBoldFn = vi.fn(() => ({ run: vi.fn() }));
const toggleItalicFn = vi.fn(() => ({ run: vi.fn() }));
const toggleStrikeFn = vi.fn(() => ({ run: vi.fn() }));
const toggleBulletListFn = vi.fn(() => ({ run: vi.fn() }));
const toggleOrderedListFn = vi.fn(() => ({ run: vi.fn() }));
const toggleBlockquoteFn = vi.fn(() => ({ run: vi.fn() }));
const setImageFn = vi.fn(() => ({ run: vi.fn() }));
const setLinkFn = vi.fn(() => ({ run: vi.fn() }));
const extendMarkRangeFn = vi.fn(() => ({
  setLink: vi.fn(() => ({ run: vi.fn() })),
  unsetLink: vi.fn(() => ({ run: vi.fn() })),
}));
const undoFn = vi.fn(() => ({ run: vi.fn() }));
const redoFn = vi.fn(() => ({ run: vi.fn() }));

const mockEditor = {
  getHTML: vi.fn(() => "<p>test</p>"),
  getText: vi.fn(() => "test"),
  commands: { setContent: vi.fn() },
  chain: vi.fn(() => ({
    focus: vi.fn(() => ({
      toggleBold: toggleBoldFn,
      toggleItalic: toggleItalicFn,
      toggleStrike: toggleStrikeFn,
      toggleBulletList: toggleBulletListFn,
      toggleOrderedList: toggleOrderedListFn,
      toggleBlockquote: toggleBlockquoteFn,
      setImage: setImageFn,
      setLink: setLinkFn,
      extendMarkRange: extendMarkRangeFn,
      undo: undoFn,
      redo: redoFn,
    })),
  })),
  isActive: vi.fn(() => false),
  getAttributes: vi.fn(() => ({})),
};

vi.mock("@tiptap/react", () => ({
  useEditor: () => mockEditor,
  EditorContent: ({ editor }: { editor: typeof mockEditor | null }) => (
    <div data-testid="editor-content">{editor ? "editor" : "null"}</div>
  ),
}));
vi.mock("@tiptap/starter-kit", () => ({ default: {} }));
vi.mock("@tiptap/extension-link", () => ({ default: { configure: () => ({}) } }));
vi.mock("@tiptap/extension-image", () => ({ default: { configure: () => ({}) } }));
vi.mock("react-i18next", () => ({
  useTranslation: () => ({ t: (key: string, fallback?: string) => fallback || key }),
}));
vi.mock("../../ui/Toast", () => ({
  useToast: () => ({ toast: toastMock }),
}));

class MockFileReader {
  onload: ((event: ProgressEvent<FileReader>) => void) | null = null;
  result: string | ArrayBuffer | null = null;
  
  readAsDataURL(_file: Blob): void {
    setTimeout(() => {
      this.result = "data:image/png;base64,mockedDataUrl";
      if (this.onload) {
        const event = { target: this } as unknown as ProgressEvent<FileReader>;
        this.onload(event);
      }
    }, 0);
  }
}

describe("RichTextEditor", () => {
  const mockOnChange = vi.fn();

  beforeEach(() => {
    vi.clearAllMocks();
    (mockEditor.isActive as MockedFunction<typeof mockEditor.isActive>).mockReturnValue(false);
    vi.stubGlobal("FileReader", MockFileReader);
  });

  it("renders toolbar with image insert button (title 'Insert image')", () => {
    render(<RichTextEditor content="" onChange={mockOnChange} />);

    const imageButton = screen.getByTitle("Insert image");
    expect(imageButton).toBeInTheDocument();
  });

  it("clicking image button triggers hidden file input click", () => {
    render(<RichTextEditor content="" onChange={mockOnChange} />);

    const imageButton = screen.getByTitle("Insert image");
    const fileInput = document.querySelector('input[type="file"]') as HTMLInputElement;
    const clickSpy = vi.spyOn(fileInput, "click");
    
    fireEvent.click(imageButton);
    
    expect(clickSpy).toHaveBeenCalledOnce();
  });

  it("file input accepts image/* files", () => {
    render(<RichTextEditor content="" onChange={mockOnChange} />);

    const fileInput = document.querySelector('input[type="file"]') as HTMLInputElement;
    
    expect(fileInput).toHaveAttribute("accept", "image/*");
  });

  it("selecting a file under 2MB reads as dataURL and calls editor.chain().focus().setImage", async () => {
    render(<RichTextEditor content="" onChange={mockOnChange} />);

    const fileInput = document.querySelector('input[type="file"]') as HTMLInputElement;
    const mockFile = new File(["test image content"], "test-image.png", { type: "image/png" });
    Object.defineProperty(mockFile, "size", { value: 1024 * 1024 });

    fireEvent.change(fileInput, { target: { files: [mockFile] } });

    await waitFor(() => {
      expect(setImageFn).toHaveBeenCalledWith({ src: "data:image/png;base64,mockedDataUrl", alt: "test-image.png" });
    });
  });

  it("selecting a file over 2MB shows toast and does NOT call setImage", () => {
    render(<RichTextEditor content="" onChange={mockOnChange} />);

    const fileInput = document.querySelector('input[type="file"]') as HTMLInputElement;
    const mockFile = new File(["test image content"], "large-image.png", { type: "image/png" });
    Object.defineProperty(mockFile, "size", { value: 3 * 1024 * 1024 });

    fireEvent.change(fileInput, { target: { files: [mockFile] } });

    expect(toastMock).toHaveBeenCalledWith(
      "error",
      "Image is too large (max 2MB). Consider using an attachment instead.",
    );
    expect(setImageFn).not.toHaveBeenCalled();
  });

  it("toolbar shows all formatting buttons (Bold, Italic, Strikethrough, Link, Image, Lists, Quote, Undo, Redo)", () => {
    render(<RichTextEditor content="" onChange={mockOnChange} />);

    expect(screen.getByTitle("compose.bold")).toBeInTheDocument();
    expect(screen.getByTitle("compose.italic")).toBeInTheDocument();
    expect(screen.getByTitle("compose.strikethrough")).toBeInTheDocument();
    expect(screen.getByTitle("compose.link")).toBeInTheDocument();
    expect(screen.getByTitle("Insert image")).toBeInTheDocument();
    expect(screen.getByTitle("compose.bulletList")).toBeInTheDocument();
    expect(screen.getByTitle("compose.orderedList")).toBeInTheDocument();
    expect(screen.getByTitle("compose.blockquote")).toBeInTheDocument();
    expect(screen.getByTitle("compose.undo")).toBeInTheDocument();
    expect(screen.getByTitle("compose.redo")).toBeInTheDocument();
  });

  it("bold button click calls toggleBold chain", () => {
    render(<RichTextEditor content="" onChange={mockOnChange} />);

    const boldButton = screen.getByTitle("compose.bold");
    fireEvent.click(boldButton);

    expect(mockEditor.chain).toHaveBeenCalled();
    expect(toggleBoldFn).toHaveBeenCalled();
  });

  it("file input is reset after selection (value = '')", async () => {
    render(<RichTextEditor content="" onChange={mockOnChange} />);

    const fileInput = document.querySelector('input[type="file"]') as HTMLInputElement;
    const mockFile = new File(["test"], "test.png", { type: "image/png" });
    Object.defineProperty(mockFile, "size", { value: 1024 });
    Object.defineProperty(fileInput, "value", {
      writable: true,
      value: "C:\\fakepath\\test.png",
    });

    fireEvent.change(fileInput, { target: { files: [mockFile] } });

    await waitFor(() => {
      expect(setImageFn).toHaveBeenCalled();
    });

    expect(fileInput.value).toBe("");
  });
});
