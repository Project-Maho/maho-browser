import { AlertDialog, AlertDialogContent, AlertDialogHeader, AlertDialogTitle, AlertDialogDescription, AlertDialogFooter, AlertDialogCancel } from "../ui/alert-dialog";
import { Button } from "../ui/Button";
import { useTranslation } from "react-i18next";

interface RecurringScopeDialogProps {
  open: boolean;
  mode: "edit" | "delete";
  onCancel: () => void;
  onSelect: (scope: "single" | "all") => void;
}

export function RecurringScopeDialog({ open, mode, onCancel, onSelect }: RecurringScopeDialogProps) {
  const { t } = useTranslation();
  const titleKey = mode === "delete" ? "calendar.recurring.deletePromptTitle" : "calendar.recurring.promptTitle";
  const messageKey = mode === "delete" ? "calendar.recurring.deletePromptMessage" : "calendar.recurring.promptMessage";

  return (
    <AlertDialog open={open} onOpenChange={(o) => { if (!o) onCancel(); }}>
      <AlertDialogContent>
        <AlertDialogHeader>
          <AlertDialogTitle>{t(titleKey)}</AlertDialogTitle>
          <AlertDialogDescription>{t(messageKey)}</AlertDialogDescription>
        </AlertDialogHeader>
        <AlertDialogFooter className="flex flex-col-reverse sm:flex-row sm:justify-end gap-2">
          <AlertDialogCancel onClick={onCancel}>{t("calendar.recurring.cancel")}</AlertDialogCancel>
          <Button variant="outline" size="sm" onClick={() => onSelect("single")}>
            {t("calendar.recurring.thisOccurrence")}
          </Button>
          <Button variant={mode === "delete" ? "destructive" : "default"} size="sm" onClick={() => onSelect("all")}>
            {t("calendar.recurring.allEvents")}
          </Button>
        </AlertDialogFooter>
      </AlertDialogContent>
    </AlertDialog>
  );
}
