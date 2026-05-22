import {cn} from '../../lib/cn.js';
import {Button} from '../../ui/button.js';

export function ModeToggle(
    {
      developerMode,
      onSetDeveloperMode,
    }: {
      developerMode: boolean;
      onSetDeveloperMode: (enabled: boolean) => void;
    }) {
  const renderButton = (label: string, enabled: boolean) => {
    const active = developerMode === enabled;
    return (
      <Button
        aria-pressed={active}
        aria-selected={active}
        className={cn(active && 'is-active')}
        role="tab"
        variant="mode-toggle"
        onClick={() => onSetDeveloperMode(enabled)}>
        {label}
      </Button>
    );
  };

  return (
    <div aria-label="AI surface mode" className="mode-toggle" role="tablist">
      {renderButton('Assistant', false)}
      {renderButton('Developer', true)}
    </div>
  );
}
