import type withDragAndDrop from "react-big-calendar/lib/addons/dragAndDrop";

export type DragAndDropWrapper = typeof withDragAndDrop;

type DragAndDropExport =
  | DragAndDropWrapper
  | { readonly default: DragAndDropWrapper };

export function resolveDragAndDrop(module: DragAndDropExport): DragAndDropWrapper {
  return typeof module === "function" ? module : module.default;
}
