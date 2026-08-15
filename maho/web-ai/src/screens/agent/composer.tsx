import { useLayoutEffect, useRef } from 'preact/hooks';
import { Icon } from '../../ui/icon';
import type { AgentPhase } from './agent-types';
import { isRunActive } from './agent-types';

interface ComposerProps {
  draft: string;
  phase: AgentPhase;
  hasMessages: boolean;
  onDraftChange: (value: string) => void;
  onSubmit: () => void;
  onStop: () => void;
  /** Flush hook for draft persistence; fired when the textarea loses focus. */
  onBlur?: () => void;
}

export function Composer({
  draft,
  phase,
  hasMessages,
  onBlur,
  onDraftChange,
  onSubmit,
  onStop,
}: ComposerProps) {
  const composingRef = useRef(false);
  const textareaRef = useRef<HTMLTextAreaElement | null>(null);
  const active = isRunActive(phase);
  const disabled = !active && draft.trim().length === 0;

  const resizeToContent = () => {
    const el = textareaRef.current;
    if (!el) return;
    el.style.height = 'auto';
    el.style.height = `${el.scrollHeight}px`;
  };

  useLayoutEffect(resizeToContent, [draft]);

  const handleKeyDown = (event: KeyboardEvent) => {
    if (event.key !== 'Enter' || event.shiftKey) return;
    if (event.isComposing || composingRef.current) return;
    event.preventDefault();
    onSubmit();
  };

  return (
    <footer class="agent-composer-wrap">
      <section class="agent-composer" data-testid="agent-composer" aria-label="Agent composer">
        <textarea
          ref={textareaRef}
          class="agent-composer-input"
          data-testid="agent-composer-input"
          aria-label={hasMessages ? 'Message' : 'Task description'}
          placeholder={hasMessages ? 'Ask a follow-up…' : 'Describe a task for the agent…'}
          rows={1}
          value={draft}
          onCompositionStart={() => {
            composingRef.current = true;
          }}
          onCompositionEnd={() => {
            composingRef.current = false;
          }}
          onBlur={() => onBlur?.()}
          onInput={(event) => onDraftChange(readTextAreaValue(event))}
          onKeyDown={handleKeyDown}
        />
        <button
          type="button"
          class={active ? 'agent-stop-button' : 'agent-send-button'}
          data-testid={active ? 'agent-stop' : 'agent-send'}
          aria-label={active ? 'Stop current run' : 'Send message'}
          disabled={disabled}
          onClick={active ? onStop : onSubmit}
        >
          <Icon name={active ? 'square' : 'arrow-up'} size={active ? 14 : 18} aria-hidden />
        </button>
      </section>
    </footer>
  );
}

function readTextAreaValue(event: Event): string {
  const target = event.currentTarget;
  if (target instanceof HTMLTextAreaElement) {
    return target.value;
  }
  return '';
}
