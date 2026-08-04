import React from "react";
import {
  Download,
  Folder,
  FolderOpen,
  Plus,
  Wand2,
} from "lucide-react";
import type { WorkspaceBadge, WorkspaceDefinition, WorkspaceRow } from "./data/workspaces";
import type { SplitPhase } from "./timeline";

export type WorkspaceBodyProps = {
  readonly workspace: WorkspaceDefinition;
  readonly activeRowLabel?: string;
  readonly recordingMode?: boolean;
  readonly splitPhase?: SplitPhase;
  readonly splitDragRatio?: number;
};

function Badge({ badge, recordingMode }: { readonly badge: WorkspaceBadge; readonly recordingMode?: boolean }): React.ReactNode {
  const size = "w-[1.25cqw] h-[1.25cqw]";
  const round = badge.rounded ? "rounded-full" : "rounded-[0.3cqw]";

  if (badge.kind === "img") {
    return <img src={badge.src} alt="" className={`${size} ${round} shrink-0 object-contain`} />;
  }

  return (
    <span
      className={`${size} ${round} grid shrink-0 place-items-center ${recordingMode ? "text-[0.6cqw]" : "text-[0.56cqw]"} font-black leading-none`}
      style={{ background: badge.background, color: badge.foreground }}
    >
      {badge.label}
    </span>
  );
}

const ROW_HEIGHT = "h-[2.55cqw]";

function Row({ row, activeRowLabel, recordingMode }: { readonly row: WorkspaceRow; readonly activeRowLabel?: string; readonly recordingMode?: boolean }): React.ReactNode {
  switch (row.kind) {
    case "folder": {
      const FolderIcon = row.open ? FolderOpen : Folder;
      return (
        <div className={`${recordingMode ? "h-[2.55cqw] text-[1cqw]" : "h-[2.15cqw] text-[0.9cqw]"} flex items-center gap-[0.6cqw] px-[0.55cqw] rounded-[0.7cqw] text-[var(--maho-text-secondary)]`}>
          <FolderIcon className="w-[1cqw] h-[1cqw] text-[var(--maho-text-muted)] shrink-0" />
          <span className="truncate">{row.label}</span>
        </div>
      );
    }
    case "tab": {
      const active = activeRowLabel === undefined ? row.active : row.label === activeRowLabel;
      return (
        <div
          data-active-row={active || undefined}
          className={`${recordingMode ? "h-[2.55cqw] text-[1cqw]" : "h-[2.15cqw] text-[0.9cqw]"} flex items-center gap-[0.6cqw] ${row.indented ? "pl-[1.6cqw] pr-[0.55cqw]" : "px-[0.55cqw]"} rounded-[0.7cqw] ${
            active
              ? "bg-[var(--maho-active-surface)] text-[var(--maho-text-primary)] font-medium"
              : "text-[var(--maho-text-secondary)] hover:bg-[var(--maho-hover-surface)]"
          }`}
        >
          <Badge badge={row.badge} recordingMode={recordingMode} />
          <span className="truncate">{row.label}</span>
        </div>
      );
    }
  }
}

function tabByLabel(rows: readonly WorkspaceRow[], label: string): Extract<WorkspaceRow, { kind: "tab" }> | undefined {
  const found = rows.find((row) => row.kind === "tab" && row.label === label);
  return found?.kind === "tab" ? found : undefined;
}

function CombinedRow({ longreads, aeon }: { readonly longreads: WorkspaceBadge; readonly aeon: WorkspaceBadge }): React.ReactNode {
  return (
    <div
      data-active-row="true"
      data-split-combined-row="true"
      className={`${ROW_HEIGHT} flex items-center rounded-[0.7cqw] bg-[var(--maho-active-surface)] text-[1cqw] font-medium text-[var(--maho-text-primary)]`}
    >
      <div className="flex min-w-0 flex-1 items-center gap-[0.5cqw] px-[0.55cqw]">
        <Badge badge={longreads} />
        <span className="truncate">Longreads</span>
      </div>
      <div className="h-[1.4cqw] w-px bg-[var(--maho-border-strong)]" />
      <div className="flex min-w-0 flex-1 items-center gap-[0.5cqw] px-[0.55cqw]">
        <Badge badge={aeon} />
        <span className="truncate">Aeon</span>
      </div>
    </div>
  );
}

function DropTargetRow({ longreads, dragRatio }: { readonly longreads: WorkspaceBadge; readonly dragRatio: number }): React.ReactNode {
  return (
    <div
      data-active-row="true"
      data-split-drop-target="true"
      className={`${ROW_HEIGHT} flex items-center rounded-[0.7cqw] bg-[var(--maho-active-surface)] text-[1cqw] font-medium text-[var(--maho-text-primary)]`}
    >
      <div className="flex min-w-0 flex-1 items-center gap-[0.5cqw] px-[0.55cqw]">
        <Badge badge={longreads} />
        <span className="truncate">Longreads</span>
      </div>
      <div className="h-[1.4cqw] w-px bg-[var(--maho-border-strong)]" />
      <div
        data-split-insertion-slot="true"
        className="h-[1.75cqw] flex-1 rounded-[0.4cqw] bg-white/8"
        style={{ opacity: 0.25 + dragRatio * 0.45 }}
      />
    </div>
  );
}

function SecondaryRows({
  workspace,
  activeRowLabel,
  recordingMode,
  splitPhase,
  splitDragRatio = 0,
}: {
  readonly workspace: WorkspaceDefinition;
  readonly activeRowLabel?: string;
  readonly recordingMode?: boolean;
  readonly splitPhase?: SplitPhase;
  readonly splitDragRatio?: number;
}): React.ReactNode {
  const longreads = tabByLabel(workspace.secondaryRows, "Longreads");
  const aeon = tabByLabel(workspace.secondaryRows, "Aeon");

  if (splitPhase === undefined || longreads === undefined || aeon === undefined) {
    return (
      <>
        {workspace.secondaryRows.map((row) => (
          <Row key={row.label} row={row} activeRowLabel={activeRowLabel} recordingMode={recordingMode} />
        ))}
      </>
    );
  }

  return (
    <>
      {workspace.secondaryRows.map((row) => {
        if (row.kind === "tab" && row.label === "Longreads") {
          if (splitPhase === "settling" || splitPhase === "settled") return <CombinedRow key="split-slot" longreads={longreads.badge} aeon={aeon.badge} />;
          if (splitPhase === "drag") return <DropTargetRow key="split-slot" longreads={longreads.badge} dragRatio={splitDragRatio} />;
          return <Row key={row.label} row={row} activeRowLabel={activeRowLabel} recordingMode={recordingMode} />;
        }
        if (row.kind === "tab" && row.label === "Aeon") {
          return splitPhase === "settling" || splitPhase === "settled" ? null : <Row key={row.label} row={row} activeRowLabel={activeRowLabel} recordingMode={recordingMode} />;
        }
        return <Row key={row.label} row={row} activeRowLabel={activeRowLabel} recordingMode={recordingMode} />;
      })}
    </>
  );
}

export const WorkspaceBody: React.FC<WorkspaceBodyProps> = ({ workspace, activeRowLabel, recordingMode = false, splitPhase, splitDragRatio }) => (
  <div className="flex h-full min-h-0 flex-col">
    <div className="mt-[0.3cqw] px-[0.55cqw] py-[0.1cqw] text-[0.85cqw] font-semibold text-[var(--maho-text-secondary)]">
      {workspace.label}
    </div>
    <div className="flex flex-col gap-[0.1cqw]">
      {workspace.primaryRows.map((row) => (
        <Row key={row.label} row={row} activeRowLabel={activeRowLabel} recordingMode={recordingMode} />
      ))}
    </div>
    <div className="h-px my-[0.4cqw] mx-[0.55cqw] bg-[var(--maho-border-subtle)]" />
    <div className="flex items-center justify-between px-[0.7cqw] py-[0.3cqw] text-[0.85cqw] text-[var(--maho-text-muted)]">
      <span className="flex items-center gap-[0.35cqw] cursor-pointer hover:text-[var(--maho-text-primary)]"><Plus className="w-[0.9cqw] h-[0.9cqw]" />New Tab</span>
      <span className="flex items-center gap-[0.35cqw] cursor-pointer hover:text-[var(--maho-text-primary)]"><Wand2 className="w-[0.9cqw] h-[0.9cqw]" />Tidy</span>
      <span className="flex items-center gap-[0.35cqw] cursor-pointer hover:text-[var(--maho-text-primary)]"><Download className="w-[0.9cqw] h-[0.9cqw]" />Clear</span>
    </div>
    <div className="flex min-h-0 flex-1 flex-col gap-[0.1cqw] overflow-hidden">
      <SecondaryRows workspace={workspace} activeRowLabel={activeRowLabel} recordingMode={recordingMode} splitPhase={splitPhase} splitDragRatio={splitDragRatio} />
    </div>
  </div>
);
