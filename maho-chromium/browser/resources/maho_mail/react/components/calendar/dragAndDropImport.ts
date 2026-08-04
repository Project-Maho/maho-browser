export type DragAndDropWrapper = (component: unknown) => unknown;

type DragAndDropExport =
  | DragAndDropWrapper
  | { readonly default: DragAndDropWrapper };

export function resolveDragAndDrop(module: DragAndDropExport): DragAndDropWrapper {
  return typeof module === "function" ? module : module.default;
}
