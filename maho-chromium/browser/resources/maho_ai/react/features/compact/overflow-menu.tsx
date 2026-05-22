import {IconButton} from '../../ui/icon-button.js';
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuTrigger,
} from '../../ui/dropdown-menu.js';

export function OverflowMenu(
    {
      onNewSession,
      onOpenDeveloperMode,
    }: {
      onNewSession: () => void;
      onOpenDeveloperMode: () => void;
    }) {
  return (
    <DropdownMenu>
      <DropdownMenuTrigger asChild>
        <IconButton
          ariaLabel="More options"
          symbol="ellipsis"
          variant="compact-icon"
        />
      </DropdownMenuTrigger>
      <DropdownMenuContent>
        <DropdownMenuItem
          onSelect={() => {
            onNewSession();
          }}>
          New chat
        </DropdownMenuItem>
        <DropdownMenuItem
          onSelect={() => {
            onOpenDeveloperMode();
          }}>
          Developer mode
        </DropdownMenuItem>
      </DropdownMenuContent>
    </DropdownMenu>
  );
}
