import { useEffect, useRef, useState } from "react";
import { useTranslation } from "react-i18next";
import { EditorContent, useEditor } from "@tiptap/react";
import StarterKit from "@tiptap/starter-kit";
import Link from "@tiptap/extension-link";
import Image from "@tiptap/extension-image";
import {
  Bold,
  ImagePlus,
  Italic,
  Strikethrough,
  Link as LinkIcon,
  List,
  ListOrdered,
  Quote,
  Redo,
  Undo,
  Check,
  X,
} from "lucide-react";
import type { ReactNode } from "react";
import { useToast } from "../ui/Toast";
import { Input } from "../ui/Input";
import { cn } from "../../lib/utils";
import { isMobile } from "../../utils/platform";

interface RichTextEditorProps {
  content: string;
  onChange: (html: string, text: string) => void;
  placeholder?: string;
  ariaLabel?: string;
  autoFocus?: boolean;
}

function ToolbarButton({
  onClick,
  active,
  children,
  title,
}: {
  onClick: () => void;
  active?: boolean;
  children: ReactNode;
  title: string;
}) {
  return (
    <button
      type="button"
      onClick={onClick}
      title={title}
      aria-label={title}
      aria-pressed={active}
      className={`mail-pressable flex h-8 w-8 items-center justify-center rounded-md focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary ${
        active
          ? "bg-primary/10 text-primary"
          : "text-muted-foreground hover:bg-surface-hover hover:text-foreground"
      }`}
    >
      {children}
    </button>
  );
}

export function RichTextEditor({ content, onChange, placeholder, ariaLabel, autoFocus }: RichTextEditorProps) {
  const { t } = useTranslation();
  const { toast } = useToast();
  const fileInputRef = useRef<HTMLInputElement>(null);
  const mobile = isMobile();
  const [showLinkInput, setShowLinkInput] = useState(false);
  const [linkValue, setLinkValue] = useState("");

  useEffect(() => {
    if (!showLinkInput) return;
    function handleKeyDown(event: KeyboardEvent) {
      if (event.key === "Escape") {
        event.preventDefault();
        event.stopPropagation();
        setShowLinkInput(false);
      }
    }
    document.addEventListener("keydown", handleKeyDown);
    return () => document.removeEventListener("keydown", handleKeyDown);
  }, [showLinkInput]);
  const editor = useEditor({
    extensions: [
      StarterKit,
      Link.configure({
        openOnClick: false,
        HTMLAttributes: { class: "text-primary underline" },
      }),
      Image.configure({
        inline: true,
        allowBase64: true,
      }),
    ],
    content,
    onUpdate: ({ editor }) => {
      onChange(editor.getHTML(), editor.getText());
    },
    editorProps: {
      attributes: {
        class:
          cn(
            "prose prose-invert prose-sm max-w-none focus:outline-none px-6 py-4 text-sm text-foreground",
            mobile ? "min-h-[200px]" : "min-h-[200px]",
          ),
        "data-placeholder": placeholder ?? "",
        "aria-label": ariaLabel ?? t("compose.richTextEditor"),
      },
    },
  });

  useEffect(() => {
    if (!editor) return;

    const currentHtml = editor.getHTML();
    if (currentHtml !== content) {
      editor.commands.setContent(content, { emitUpdate: false });
    }
  }, [content, editor]);

  useEffect(() => {
    if (autoFocus && editor) {
      editor.commands.focus("end");
    }
  }, [autoFocus, editor]);

  if (!editor) return null;

  const showStrike = !mobile;
  const showImage = true;
  const showBlockquote = !mobile;
  const showUndo = !mobile;
  const showRedo = !mobile;

  function handleLink() {
    const previousUrl = editor.getAttributes("link").href;
    if (mobile) {
      setLinkValue(previousUrl ?? "");
      setShowLinkInput(true);
      return;
    }

    const url = window.prompt(t("compose.urlPrompt"), previousUrl);
    if (url === null) return;
    if (url === "") {
      editor.chain().focus().extendMarkRange("link").unsetLink().run();
    } else {
      editor.chain().focus().extendMarkRange("link").setLink({ href: url }).run();
    }
  }

  function applyLink() {
    const url = linkValue.trim();
    if (url === "") {
      editor.chain().focus().extendMarkRange("link").unsetLink().run();
      setShowLinkInput(false);
      return;
    }

    editor.chain().focus().extendMarkRange("link").setLink({ href: url }).run();
    setShowLinkInput(false);
  }

  function handleInsertImage() {
    fileInputRef.current?.click();
  }

  function handleFileChange(event: React.ChangeEvent<HTMLInputElement>) {
    const file = event.target.files?.[0];
    if (!file) return;

    const MAX_SIZE = 2 * 1024 * 1024; // 2MB
    if (file.size > MAX_SIZE) {
      toast("error", t("compose.imageTooLarge", "Image is too large (max 2MB). Consider using an attachment instead."));
      event.target.value = "";
      return;
    }

    const reader = new FileReader();
    reader.onload = (e) => {
      const dataUrl = e.target?.result as string;
      if (dataUrl) {
        const alt = window.prompt(t("compose.imageAltPrompt")) || file.name;
        editor.chain().focus().setImage({ src: dataUrl, alt }).run();
      }
    };
    reader.readAsDataURL(file);
    event.target.value = "";
  }

  return (
    <div className="flex flex-1 flex-col">
      <div className={cn("border-b border-border/60 px-1 py-1.5", mobile && "py-2")} role="toolbar" aria-label={t("compose.formattingOptions")}>
        <div className="flex flex-wrap items-center gap-0.5">
          <ToolbarButton
            onClick={() => editor.chain().focus().toggleBold().run()}
            active={editor.isActive("bold")}
            title={t("compose.bold")}
          >
            <Bold size={16} />
          </ToolbarButton>
          <ToolbarButton
            onClick={() => editor.chain().focus().toggleItalic().run()}
            active={editor.isActive("italic")}
            title={t("compose.italic")}
          >
            <Italic size={16} />
          </ToolbarButton>
          {showStrike && (
            <ToolbarButton
              onClick={() => editor.chain().focus().toggleStrike().run()}
              active={editor.isActive("strike")}
              title={t("compose.strikethrough")}
            >
              <Strikethrough size={16} />
            </ToolbarButton>
          )}

          {!mobile && <div className="mx-1 h-5 w-px bg-border" />}

          <ToolbarButton onClick={handleLink} active={editor.isActive("link")} title={t("compose.link")}>
            <LinkIcon size={16} />
          </ToolbarButton>
          {showImage && (
            <ToolbarButton onClick={handleInsertImage} title={t("compose.insertImage", "Insert image")}>
              <ImagePlus size={16} />
            </ToolbarButton>
          )}

          {!mobile && <div className="mx-1 h-5 w-px bg-border" />}

          <ToolbarButton
            onClick={() => editor.chain().focus().toggleBulletList().run()}
            active={editor.isActive("bulletList")}
            title={t("compose.bulletList")}
          >
            <List size={16} />
          </ToolbarButton>
          <ToolbarButton
            onClick={() => editor.chain().focus().toggleOrderedList().run()}
            active={editor.isActive("orderedList")}
            title={t("compose.orderedList")}
          >
            <ListOrdered size={16} />
          </ToolbarButton>
          {showBlockquote && (
            <ToolbarButton
              onClick={() => editor.chain().focus().toggleBlockquote().run()}
              active={editor.isActive("blockquote")}
              title={t("compose.blockquote")}
            >
              <Quote size={16} />
            </ToolbarButton>
          )}

          {!mobile && <div className="mx-1 h-5 w-px bg-border" />}

          {showUndo && (
            <ToolbarButton onClick={() => editor.chain().focus().undo().run()} title={t("compose.undo")}>
              <Undo size={16} />
            </ToolbarButton>
          )}
          {showRedo && (
            <ToolbarButton onClick={() => editor.chain().focus().redo().run()} title={t("compose.redo")}>
              <Redo size={16} />
            </ToolbarButton>
          )}
        </div>
        {mobile && showLinkInput && (
          <div
            className="mt-2 flex items-end gap-2 rounded-xl border border-border bg-card/40 p-2"
            onKeyDown={(e) => {
              if (e.key === "Escape") {
                e.preventDefault();
                e.stopPropagation();
                setShowLinkInput(false);
              }
            }}
          >
            <Input
              value={linkValue}
              onChange={setLinkValue}
              placeholder={t("compose.urlPrompt")}
              className="h-11"
            />
            <button
              type="button"
              onClick={applyLink}
              className="flex h-11 w-11 items-center justify-center rounded-lg bg-primary text-primary-foreground transition-colors hover:bg-primary/90"
              aria-label={t("common.save")}
            >
              <Check size={16} />
            </button>
            <button
              type="button"
              onClick={() => setShowLinkInput(false)}
              className="mail-pressable flex h-11 w-11 items-center justify-center rounded-lg text-muted-foreground hover:bg-surface-hover hover:text-foreground"
              aria-label={t("common.cancel", "Cancel")}
            >
              <X size={16} />
            </button>
          </div>
        )}
        <input
          ref={fileInputRef}
          type="file"
          accept="image/*"
          onChange={handleFileChange}
          className="hidden"
        />
      </div>

      <div className="flex-1 overflow-y-auto">
        <EditorContent editor={editor} />
      </div>
    </div>
  );
}
