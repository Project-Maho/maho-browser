import { useMemo, useRef, useEffect, useState } from "react";
import { Paperclip, ExternalLink, Download } from "lucide-react";
import type { EmailDetail } from "../../types";
import { useSettings } from "../../hooks/useSettings";
import { prepareIframeHtml } from "../../utils/emailHtmlProcessing";
import { isDangerousAttachment } from "../../utils/attachmentSafety";
import { MAHO_SINK_MARKER } from "../../utils/sanitizeHtml";
import { AttachmentPreview } from "./EmailContent";
import { ViewSettingsMenu } from "../email/ViewSettingsMenu";
import { useTranslation as useI18nTranslation } from "react-i18next";
import { useConfirm } from "../ui/ConfirmDialog";
import { useToast } from "../ui/Toast";
import * as api from "../../api";
import { formatFileSize } from "../ui/utils";
import { openExternalUrl } from "../../utils/openExternal.js";

interface EmailBodyViewProps {
  emailDetail: EmailDetail;
  pgpDecryptedText?: string | null;
  smimeDecryptedText?: string | null;
  translatedText?: string | null;
  translatedHtml?: string | null;
  showOriginalBody?: boolean;
  onToggleShowOriginal?: () => void;
  onEmailAddressClick?: (email: string) => void;
}

export function EmailBodyView({
  emailDetail,
  pgpDecryptedText,
  smimeDecryptedText,
  translatedText,
  translatedHtml,
  showOriginalBody,
  onToggleShowOriginal,
  onEmailAddressClick,
}: EmailBodyViewProps) {
  const { t } = useI18nTranslation();
  const confirm = useConfirm();
  const { toast } = useToast();
  const {
    blockRemoteImages,
    blockTrackers,
    emailBodyFontFamily,
    emailBodyFontSize,
  } = useSettings();
  const [imagesBlocked, setImagesBlocked] = useState(true);
  const [htmlDarkMode, setHtmlDarkMode] = useState(true);
  const iframeRef = useRef<HTMLIFrameElement>(null);
  const [iframeHeight, setIframeHeight] = useState(400);

  const { email, attachments = [] } = emailDetail;

  useEffect(() => {
    setImagesBlocked(true);
  }, [email.id]);

  const shouldBlockImages = blockRemoteImages && imagesBlocked;
  const htmlToRender = (!showOriginalBody && translatedHtml) ? translatedHtml : email.body_html;

  const processedHtmlInfo = useMemo(() => {
    if (!htmlToRender) {
      return { html: "", blockedCount: 0, trackersBlocked: 0, trackerDomains: [] as string[] };
    }
    return prepareIframeHtml(htmlToRender, {
      blockImages: shouldBlockImages,
      blockTrackers: blockTrackers,
      isDark: htmlDarkMode,
      fontFamily: emailBodyFontFamily,
      fontSize: emailBodyFontSize,
    });
  }, [
    htmlToRender,
    shouldBlockImages,
    blockTrackers,
    htmlDarkMode,
    emailBodyFontFamily,
    emailBodyFontSize,
  ]);

  // Toggle theme class on the iframe HTML element when htmlDarkMode changes
  useEffect(() => {
    const iframe = iframeRef.current;
    if (iframe && iframe.contentDocument && iframe.contentDocument.documentElement) {
      iframe.contentDocument.documentElement.classList.toggle("dark", htmlDarkMode);
    }
  }, [htmlDarkMode, processedHtmlInfo]);

  // Handle postMessage events for iframe height resize (validated and clamped)
  useEffect(() => {
    const handleMessage = (event: MessageEvent) => {
      if (event.source !== iframeRef.current?.contentWindow) {
        return;
      }
      if (event.data && event.data.type === "resize" && typeof event.data.height === "number") {
        const clampedHeight = Math.min(Math.max(0, event.data.height), 50000);
        setIframeHeight(clampedHeight);
      }
    };
    window.addEventListener("message", handleMessage);
    return () => window.removeEventListener("message", handleMessage);
  }, []);

  // Observe and adjust iframe height directly from parent (foolproof fallback)
  useEffect(() => {
    const iframe = iframeRef.current;
    if (!iframe) return;

    let observer: ResizeObserver | null = null;
    let clickHandler: ((e: Event) => void) | null = null;

    const updateHeight = () => {
      if (iframe.contentDocument && iframe.contentDocument.body) {
        const docElem = iframe.contentDocument.documentElement;
        const bodyElem = iframe.contentDocument.body;
        const height = docElem.scrollHeight || bodyElem.scrollHeight;
        setIframeHeight(Math.min(Math.max(height, 100), 50000));
      }
    };

    const handleLoad = () => {
      updateHeight();
      if (iframe.contentDocument && iframe.contentDocument.body) {
        if (observer) {
          observer.disconnect();
        }
        observer = new ResizeObserver(() => {
          updateHeight();
        });
        observer.observe(iframe.contentDocument.body);

        if (clickHandler) {
          iframe.contentDocument.removeEventListener("click", clickHandler);
          iframe.contentDocument.removeEventListener("auxclick", clickHandler);
        }
        clickHandler = (e: Event) => {
          const target = e.target as HTMLElement | null;
          const anchor = target?.closest("a");
          if (!anchor) return;
          e.preventDefault();
          if (e.type === "auxclick" && "button" in e && e.button !== 1) return;
          if (anchor.classList.contains("maho-email-link")) {
            const email = anchor.getAttribute("data-email");
            if (email) onEmailAddressClick?.(email);
            return;
          }
          const href = anchor.getAttribute("href") ?? anchor.getAttribute("xlink:href");
          if (!href || !URL.canParse(href)) return;
          const url = new URL(href);
          if (["http:", "https:", "mailto:"].includes(url.protocol)) {
            openExternalUrl(url.href);
          }
        };
        iframe.contentDocument.addEventListener("click", clickHandler);
        iframe.contentDocument.addEventListener("auxclick", clickHandler);
      }
    };

    iframe.addEventListener("load", handleLoad);
    handleLoad();

    return () => {
      iframe.removeEventListener("load", handleLoad);
      if (observer) {
        observer.disconnect();
      }
      if (clickHandler && iframe.contentDocument) {
        iframe.contentDocument.removeEventListener("click", clickHandler);
        iframe.contentDocument.removeEventListener("auxclick", clickHandler);
      }
    };
  }, [processedHtmlInfo, onEmailAddressClick]);

  return (
    <div className="flex-1 overflow-y-auto px-6 py-4">
      {pgpDecryptedText ? (
        <div className="max-w-[72ch] whitespace-pre-wrap text-[15px] leading-7 text-foreground">
          {pgpDecryptedText}
        </div>
      ) : smimeDecryptedText ? (
        <div className="max-w-[72ch] whitespace-pre-wrap text-[15px] leading-7 text-foreground">
          {smimeDecryptedText}
        </div>
      ) : translatedHtml && !showOriginalBody ? (
        (() => {
          const { html: wrappedHtml, blockedCount, trackersBlocked, trackerDomains } = processedHtmlInfo;
          return (
            <>
              <div className="flex items-center justify-end py-1">
                <ViewSettingsMenu
                  darkMode={htmlDarkMode}
                  onToggleDarkMode={() => setHtmlDarkMode(!htmlDarkMode)}
                  imagesBlocked={shouldBlockImages}
                  blockedImageCount={blockedCount}
                  onToggleImagesBlocked={() => setImagesBlocked(!imagesBlocked)}
                  trackerCount={trackersBlocked}
                  trackerDomains={trackerDomains}
                  hasActiveTranslation={Boolean(translatedHtml || translatedText)}
                  showTranslation={!showOriginalBody}
                  onToggleShowTranslation={() => onToggleShowOriginal?.()}
                />
              </div>
              <iframe
                ref={iframeRef}
                srcDoc={`${wrappedHtml}${MAHO_SINK_MARKER}`}
                title="Email body"
                className="w-full border-0"
                sandbox="allow-same-origin"
                style={{ height: `${iframeHeight}px`, borderRadius: "8px" }}
              />
            </>
          );
        })()
      ) : translatedText && !showOriginalBody ? (
        <div className="whitespace-pre-wrap px-6 py-3 text-sm leading-relaxed text-foreground">
          {translatedText}
        </div>
      ) : email.body_html ? (
        (() => {
          const { html: wrappedHtml, blockedCount, trackersBlocked, trackerDomains } = processedHtmlInfo;
          return (
            <>
              <div className="flex items-center justify-end py-1">
                <ViewSettingsMenu
                  darkMode={htmlDarkMode}
                  onToggleDarkMode={() => setHtmlDarkMode(!htmlDarkMode)}
                  imagesBlocked={shouldBlockImages}
                  blockedImageCount={blockedCount}
                  onToggleImagesBlocked={() => setImagesBlocked(!imagesBlocked)}
                  trackerCount={trackersBlocked}
                  trackerDomains={trackerDomains}
                  hasActiveTranslation={Boolean(translatedHtml || translatedText)}
                  showTranslation={!showOriginalBody}
                  onToggleShowTranslation={() => onToggleShowOriginal?.()}
                />
              </div>
              <iframe
                ref={iframeRef}
                srcDoc={`${wrappedHtml}${MAHO_SINK_MARKER}`}
                title="Email body"
                className="w-full border-0"
                sandbox="allow-same-origin"
                style={{ height: `${iframeHeight}px`, borderRadius: "8px" }}
              />
            </>
          );
        })()
      ) : (
        <div className="whitespace-pre-wrap text-sm leading-relaxed text-muted-foreground">
          {email.body_text ?? email.snippet}
        </div>
      )}

      {/* Attachments */}
      {attachments.length > 0 && (
        <div className="mt-6 border-t border-border pt-4">
          <p className="mb-2 flex items-center gap-1.5 text-xs font-medium text-muted-foreground">
            <Paperclip size={12} />
            {attachments.length} {t("email.attachment", { count: attachments.length })}
          </p>
          <div className="flex flex-wrap gap-3">
            {attachments.map((att) => (
              <div
                key={att.id}
                className="flex min-w-[14rem] max-w-sm flex-1 flex-col overflow-hidden rounded-lg border border-border bg-card/50 text-sm transition-colors hover:border-foreground/20 animate-scale-in"
              >
                <AttachmentPreview attachment={att} email={email} />
                <div className="flex items-center gap-2 px-3 py-2">
                  <Paperclip className="h-4 w-4 shrink-0 text-muted-foreground" />
                  <div className="min-w-0 flex-1">
                    <p className="break-words text-muted-foreground font-medium leading-snug">
                      {att.filename ?? "attachment"}
                    </p>
                    <p className="text-[10px] text-muted-foreground/60">
                      ({formatFileSize(att.size)})
                    </p>
                  </div>
                </div>
                <div className="flex border-t border-border mt-auto">
                  <button
                    type="button"
                    onClick={async () => {
                      const filename = att.filename ?? "attachment";

                      if (isDangerousAttachment({ filename, mimeType: att.mime_type })) {
                        const confirmed = await confirm({
                          title: t("email.dangerousAttachmentTitle"),
                          message: t("email.dangerousAttachmentMessage", { filename }),
                          confirmLabel: t("email.openAnyway"),
                          danger: true,
                        });
                        if (!confirmed) return;
                      }

                      try {
                        const capabilityToken = await api.downloadAttachment(
                          email.account_id,
                          email.uid,
                          email.folder_id,
                          att.part_id,
                          att.filename ?? "attachment",
                        );
                        await api.openAttachment(capabilityToken);
                      } catch (err) {
                        toast("error", err instanceof Error ? err.message : "Failed to open attachment");
                      }
                    }}
                    className="flex h-9 flex-1 items-center justify-center gap-1.5 text-xs text-muted-foreground transition-colors hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-inset focus-visible:ring-ring border-r border-border"
                    title={t("email.openAttachment")}
                  >
                    <ExternalLink className="h-3.5 w-3.5" />
                    {t("email.open")}
                  </button>
                  <button
                    type="button"
                    onClick={async () => {
                      try {
                        const capabilityToken = await api.downloadAttachment(
                          email.account_id,
                          email.uid,
                          email.folder_id,
                          att.part_id,
                          att.filename ?? "attachment",
                        );
                        await api.saveAttachment(capabilityToken);
                        toast("success", `Saved ${att.filename ?? "attachment"}`);
                      } catch (err) {
                        toast("error", err instanceof Error ? err.message : "Failed to download attachment");
                      }
                    }}
                    className="flex h-9 flex-1 items-center justify-center gap-1.5 text-xs text-muted-foreground transition-colors hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-inset focus-visible:ring-ring"
                    title={t("email.downloadAttachment")}
                  >
                    <Download className="h-3.5 w-3.5" />
                    {t("email.download")}
                  </button>
                </div>
              </div>
            ))}
          </div>
        </div>
      )}
    </div>
  );
}
