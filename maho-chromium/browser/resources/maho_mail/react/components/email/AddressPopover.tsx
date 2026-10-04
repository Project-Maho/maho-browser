import { type ReactNode, useEffect, useState } from "react";
import { useTranslation } from "react-i18next";
import {
  Copy,
  PencilLine,
  Search,
  Mail as MailIcon,
  Zap,
  Send,
  Bell,
  ThumbsDown,
  ChevronsUpDown,
} from "lucide-react";

import { useClipboard } from "../../hooks/useClipboard";
import { useToast } from "../ui/Toast";
import { Avatar } from "../ui/Avatar";
import { Switch } from "../ui/Switch";
import {
  Popover,
  PopoverContent,
  PopoverTrigger,
} from "../ui/popover";
import * as api from "../../api";

export interface AddressPopoverProps {
  address: string;
  name?: string | null;
  accountId?: string;
  category?: string | null;
  onCompose?: (email: string) => void;
  children: ReactNode;
}

/**
 * Set a controlled-input value via the native HTMLInputElement setter so React's
 * internal _valueTracker is bypassed cleanly: the subsequent `input` event then
 * triggers React's onChange and the parent's controlled setState fires. Without
 * this, `input.value = "..."` plus dispatchEvent is a no-op for React-controlled
 * inputs because the tracker matches the new value by the time onChange runs.
 */
function setReactInputValue(input: HTMLInputElement, value: string): void {
  const setter = Object.getOwnPropertyDescriptor(
    window.HTMLInputElement.prototype,
    "value",
  )?.set;
  if (setter) {
    setter.call(input, value);
  } else {
    input.value = value;
  }
}

function fillMailSearch(email: string): boolean {
  const input = document.querySelector<HTMLInputElement>(
    "[data-mail-search-input]",
  );
  if (!input) {
    return false;
  }
  setReactInputValue(input, `from:${email} `);
  input.focus();
  input.dispatchEvent(new Event("input", { bubbles: true }));
  return true;
}

export function AddressPopover({
  address,
  name,
  accountId,
  category,
  onCompose,
  children,
}: AddressPopoverProps) {
  const [open, setOpen] = useState(false);
  const [vipInfo, setVipInfo] = useState<{ contactId: string; isVip: boolean } | null>(null);
  const [vipLoading, setVipLoading] = useState(false);
  const { copy } = useClipboard();
  const { toast } = useToast();
  const { t } = useTranslation();

  const close = () => setOpen(false);
  const stubToast = () =>
    toast("info", t("email.comingSoon"), t("email.featureNotAvailable"));

  useEffect(() => {
    if (!open || !accountId) {
      setVipInfo(null);
      return;
    }
    let cancelled = false;
    void (async () => {
      try {
        const contacts = await api.searchContacts(accountId, address, 5);
        if (cancelled) return;
        const lower = address.toLowerCase();
        const match = contacts.find((c) => c.email.toLowerCase() === lower);
        setVipInfo(match ? { contactId: match.id, isVip: match.is_vip } : null);
      } catch {
        if (!cancelled) setVipInfo(null);
      }
    })();
    return () => {
      cancelled = true;
    };
  }, [open, address, accountId]);

  const handleVipToggle = async () => {
    if (!vipInfo || vipLoading) {
      stubToast();
      return;
    }
    const prev = vipInfo.isVip;
    setVipInfo({ ...vipInfo, isVip: !prev });
    setVipLoading(true);
    try {
      await api.toggleVip(vipInfo.contactId);
    } catch (err) {
      setVipInfo({ ...vipInfo, isVip: prev });
      const message = err instanceof Error ? err.message : "Failed to toggle VIP";
      toast("error", message);
    } finally {
      setVipLoading(false);
    }
  };

  const handleBlockSender = async () => {
    if (!accountId) {
      stubToast();
      return;
    }
    close();
    try {
      await api.createMailRule({
        account_id: accountId,
        name: `Block ${address}`,
        conditions: [{ field: "from", operator: "contains", value: address }],
        actions: [{ type: "delete" }],
      });
      toast("success", t("email.senderBlocked"), address);
    } catch (err) {
      const message = err instanceof Error ? err.message : "Failed to block sender";
      toast("error", message);
    }
  };

  return (
    <Popover open={open} onOpenChange={setOpen}>
      <PopoverTrigger asChild>{children}</PopoverTrigger>
      <PopoverContent
        align="start"
        sideOffset={6}
        collisionPadding={8}
        className="w-[360px] max-w-[calc(100vw-1rem)] rounded-2xl p-0 overflow-hidden"
      >
        <div className="flex items-start gap-4 p-5">
          <Avatar
            name={name ?? address}
            email={address}
            size="xl"
            className="shrink-0"
          />
          <div className="min-w-0 flex-1">
            <div className="truncate text-lg font-semibold text-foreground">
              {name ?? address}
            </div>
            <div className="mt-0.5 flex min-w-0 items-center gap-1">
              <span
                className="min-w-0 flex-1 truncate text-sm text-primary"
                title={address}
              >
                {address}
              </span>
              <button
                type="button"
                onClick={() => {
                  void copy(address);
                }}
                className="mail-pressable flex h-8 w-8 items-center justify-center rounded-lg text-muted-foreground hover:bg-surface-hover hover:text-foreground focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-ring shrink-0"
                aria-label={t("email.copyEmailAddress")}
                title={t("email.copyEmailAddress")}
              >
                <Copy className="h-4 w-4" />
              </button>
            </div>
            <button
              type="button"
              onClick={stubToast}
              aria-label={t("email.changeCategory")}
              className="mt-2 inline-flex items-center gap-1 rounded-full bg-muted px-2.5 py-1 text-[13px] font-medium text-foreground transition-colors hover:bg-surface-selected focus-visible:outline-none focus-visible:ring-2 focus-visible:ring-primary"
            >
              <span>{category ?? t("email.uncategorized")}</span>
              <ChevronsUpDown className="h-3 w-3 text-muted-foreground" />
            </button>
          </div>
        </div>

        <div className="ml-[84px] border-t border-border" />

        <ul className="py-1">
          <li>
            <button
              type="button"
              onClick={() => {
                close();
                if (onCompose) {
                  onCompose(address);
                } else {
                  stubToast();
                }
              }}
              className="flex w-full items-center gap-3.5 px-5 py-3 text-left text-[15px] transition-colors hover:bg-surface-hover focus-visible:bg-surface-hover focus-visible:outline-none"
            >
              <PencilLine className="h-5 w-5 shrink-0 text-muted-foreground" />
              <span className="flex-1 text-foreground">{t("email.composeEmail")}</span>
            </button>
          </li>
          <li>
            <button
              type="button"
              onClick={() => {
                close();
                const ok = fillMailSearch(address);
                if (!ok) {
                  toast(
                    "info",
                    t("email.searchUnavailable"),
                    t("email.searchUnavailableSubtitle"),
                  );
                }
              }}
              className="flex w-full items-start gap-3.5 px-5 py-3 text-left text-[15px] transition-colors hover:bg-surface-hover focus-visible:bg-surface-hover focus-visible:outline-none"
            >
              <Search className="mt-0.5 h-5 w-5 shrink-0 text-muted-foreground" />
              <span className="min-w-0 flex-1">
                <span className="block text-foreground">{t("email.searchEmailsFrom")}</span>
                <span className="block truncate text-[13px] text-muted-foreground">
                  {address}
                </span>
              </span>
            </button>
          </li>
          <li>
            <button
              type="button"
              onClick={stubToast}
              className="flex w-full items-start gap-3.5 px-5 py-3 text-left text-[15px] transition-colors hover:bg-surface-hover focus-visible:bg-surface-hover focus-visible:outline-none"
            >
              <MailIcon className="mt-0.5 h-5 w-5 shrink-0 text-muted-foreground" />
              <span className="min-w-0 flex-1">
                <span className="block text-foreground">{t("email.groupEmails")}</span>
                <span className="block text-[13px] text-muted-foreground">
                  {t("email.groupEmailsSubtitle")}
                </span>
              </span>
              <span className="inline-flex shrink-0 items-center gap-1 rounded-full bg-muted px-2 py-0.5 text-[13px] text-muted-foreground">
                {t("email.off")}
                <ChevronsUpDown className="h-3 w-3" />
              </span>
            </button>
          </li>
          <li className="flex items-center gap-3.5 px-5 py-3 text-[15px]">
            <Zap className="h-5 w-5 shrink-0 text-muted-foreground" />
            <span className="flex-1 text-foreground">{t("email.markAsPriority")}</span>
            <Switch
              checked={vipInfo?.isVip ?? false}
              disabled={!vipInfo || vipLoading}
              onCheckedChange={() => void handleVipToggle()}
              aria-label={t("email.markAsPriority")}
            />
          </li>
          <li className="flex items-center gap-3.5 px-5 py-3 text-[15px]">
            <Send className="h-5 w-5 shrink-0 text-muted-foreground" />
            <span className="flex-1 text-foreground">{t("email.showInPrimaryList")}</span>
            <Switch
              checked={false}
              onCheckedChange={() => stubToast()}
              aria-label={t("email.showInPrimaryList")}
            />
          </li>
          <li className="flex items-center gap-3.5 px-5 py-3 text-[15px]">
            <Bell className="h-5 w-5 shrink-0 text-muted-foreground" />
            <span className="flex-1 text-foreground">{t("email.notifications")}</span>
            <Switch
              checked={false}
              onCheckedChange={() => stubToast()}
              aria-label={t("email.notifications")}
            />
          </li>
          <li>
            <button
              type="button"
              onClick={() => void handleBlockSender()}
              className="flex w-full items-center gap-3.5 px-5 py-3 text-left text-[15px] text-destructive hover:bg-destructive/10 focus-visible:bg-destructive/10 focus-visible:outline-none"
            >
              <ThumbsDown className="h-5 w-5 shrink-0" />
              <span className="flex-1">{t("email.blockSender")}</span>
            </button>
          </li>
        </ul>
      </PopoverContent>
    </Popover>
  );
}
