import type { ReactNode } from "react";

import { Drawer, DrawerContent, DrawerHeader, DrawerTitle } from "../ui/drawer";
import { cn } from "../../lib/utils";

interface BottomSheetProps {
  isOpen: boolean;
  onClose: () => void;
  title?: string;
  children: ReactNode;
}

export function BottomSheet({ isOpen, onClose, title, children }: BottomSheetProps) {
  return (
    <Drawer
      open={isOpen}
      onOpenChange={(open) => {
        if (!open) {
          onClose();
        }
      }}
    >
      <DrawerContent
        className={cn(
          "md:hidden flex max-h-[min(80vh,40rem)] flex-col overflow-hidden rounded-t-3xl border border-b-0 border-border bg-card p-0 shadow-2xl",
        )}
      >
        {title ? (
          <DrawerHeader className="border-b border-border px-4 pb-3 pt-3">
            <DrawerTitle className="text-sm font-semibold text-foreground">{title}</DrawerTitle>
          </DrawerHeader>
        ) : null}
        <div
          className="overflow-y-auto overscroll-contain px-2 py-2"
          style={{
            paddingBottom: "env(safe-area-inset-bottom)",
            paddingLeft: "env(safe-area-inset-left)",
            paddingRight: "env(safe-area-inset-right)",
          }}
        >
          {children}
        </div>
      </DrawerContent>
    </Drawer>
  );
}
