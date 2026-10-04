import {ArrowLeft, ExternalLink, Loader2, Pipette} from '@icons/lucide';
import {Button} from '@ui/button';
import {useCodeMirror} from '../use-codemirror.js';

export interface CodeRouteProps {
  readonly active: boolean;
  readonly css: string;
  readonly disabled?: boolean;
  readonly editorFocusRequest?: number;
  readonly inspectorPending?: boolean;
  readonly onBack: () => void;
  readonly onCssChange: (css: string) => void;
  readonly onOpenInspector: () => void;
  readonly onPickerActiveChange: (active: boolean) => void;
  readonly pickerActive: boolean;
}

export function CodeRoute(
    {
      active,
      css,
      disabled = false,
      editorFocusRequest = 0,
      inspectorPending = false,
      onBack,
      onCssChange,
      onOpenInspector,
      onPickerActiveChange,
      pickerActive,
    }: CodeRouteProps) {
  const {hostRef, mounted} = useCodeMirror({
    active,
    disabled,
    focusRequest: editorFocusRequest,
    onChange: onCssChange,
    value: css,
  });
  const pickerLegacyAttribute = {enabled: pickerActive ? 'true' : 'false'};

  return (
    <main
      aria-label="Code editor"
      className="code-mode h-[582px] w-[452px] overflow-hidden bg-background text-foreground"
      hidden={!active}
      id="zen-boost-code-editor-root">
      <div
        className="code-mode grid h-[582px] min-h-0 grid-rows-[40px_482px_60px]"
        id="code-editor-container">
        <header
          className="flex h-[40px] items-center border-b border-[#ededef] bg-[#f6f6f8] [-webkit-app-region:drag]"
          id="zen-boost-code-top-bar">
          <Button
            aria-label="Back to Boost"
            className="subviewbutton m-[5px] h-[30px] w-[60px] justify-start gap-1.5 bg-transparent px-0 text-xs font-normal text-[#3a3a3b] shadow-none opacity-60 hover:bg-[#e3e3e6] hover:opacity-80 [-webkit-app-region:no-drag]"
            disabled={disabled}
            id="zen-boost-back"
            size="sm"
            type="button"
            variant="ghost"
            onClick={onBack}>
            <ArrowLeft aria-hidden="true" className="boost-control-icon size-3.5" />
            <span id="zen-boost-back-text">Back</span>
          </Button>
        </header>

        <section
          aria-busy={!mounted}
          aria-disabled={disabled}
          aria-label="Custom CSS"
          className="h-[482px] min-h-0 w-full overflow-hidden bg-[#fcfcfe] [-webkit-app-region:no-drag]"
          id="zen-boost-code-editor">
          <div
            aria-disabled={disabled}
            className="h-full w-full overflow-hidden focus-within:ring-1 focus-within:ring-inset focus-within:ring-ring [-webkit-app-region:no-drag]"
            id="custom-css-editor"
            inert={disabled}
            ref={hostRef}
          />
        </section>

        <footer
          className="flex h-[60px] items-center gap-2.5 border-t border-[#ededef] bg-[#f6f6f8] p-2.5 [-webkit-app-region:no-drag]"
          id="zen-boost-code-bottom-bar">
          <Button
            {...pickerLegacyAttribute}
            aria-pressed={pickerActive}
            aria-label="Pick selector"
            className="mod-button toggleable-button size-10 shrink-0 bg-[#ebebed] p-0 text-[#3a3a3b] shadow-none hover:bg-[#ebebed] hover:opacity-[0.85] [-webkit-app-region:no-drag]"
            disabled={disabled}
            id="zen-boost-css-picker"
            size="icon"
            title="Pick selector"
            type="button"
            variant="secondary"
            onClick={() => onPickerActiveChange(!pickerActive)}>
            <Pipette aria-hidden="true" className="boost-control-icon size-4" />
            <span className="sr-only">Pick selector</span>
          </Button>
          <Button
            aria-busy={inspectorPending}
            aria-label={inspectorPending ? 'Opening Inspector' : 'Open Inspector'}
            className="mod-button size-10 shrink-0 bg-[#ebebed] p-0 text-[#3a3a3b] shadow-none hover:bg-[#ebebed] hover:opacity-[0.85] [-webkit-app-region:no-drag]"
            disabled={disabled || inspectorPending}
            id="zen-boost-css-inspector"
            size="icon"
            title="Open Inspector"
            type="button"
            variant="secondary"
            onClick={onOpenInspector}>
            {inspectorPending ? (
              <Loader2
                aria-hidden="true"
                className="boost-control-icon size-4 animate-spin motion-reduce:animate-none"
              />
            ) : (
              <ExternalLink aria-hidden="true" className="boost-control-icon size-4" />
            )}
            <span className="sr-only">
              {inspectorPending ? 'Opening Inspector' : 'Open Inspector'}
            </span>
          </Button>
        </footer>
      </div>
    </main>
  );
}
