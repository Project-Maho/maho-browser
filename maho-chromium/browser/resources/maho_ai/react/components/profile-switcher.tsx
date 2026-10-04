import React from 'react';
import {
  DropdownMenu,
  DropdownMenuContent,
  DropdownMenuItem,
  DropdownMenuTrigger,
} from '@ui/dropdown-menu';
import {Button} from '@ui/button';
import {User, ChevronDown} from '@icons/lucide';
import {useAppState} from '../hooks/use-app-state.js';
import {MahoAiStore} from '../../store.js';

export function ProfileSwitcher({store}: {store: MahoAiStore}) {
  const state = useAppState(store);
  const {profiles, activeProfileId, activeWorkspace} = state;

  const activeProfile = profiles.find(p => p.id === activeProfileId);

  const handleSwitch = (profileId: string) => {
    if (activeWorkspace) {
      void store.switchWorkspaceProfile(activeWorkspace.id, profileId);
    }
  };

  return (
    <DropdownMenu>
      <DropdownMenuTrigger asChild>
        <Button
          aria-label={`Select profile: ${activeProfile ? activeProfile.name : 'Select Profile'}`}
          className="flex h-7 min-w-0 max-w-[9rem] shrink items-center gap-1 rounded-lg px-1.5 text-[11.5px] font-medium tracking-[-0.005em] hover:bg-surface-hover"
          size="sm"
          title={activeProfile ? activeProfile.name : 'Select Profile'}
          variant="ghost">
          <User aria-hidden="true" className="size-3.5 shrink-0 text-muted-foreground" />
          <span className="min-w-0 truncate">{activeProfile ? activeProfile.name : 'Select Profile'}</span>
          <ChevronDown aria-hidden="true" className="size-3 shrink-0 text-muted-foreground opacity-60" />
        </Button>
      </DropdownMenuTrigger>
      <DropdownMenuContent align="start" className="w-[180px] p-1.5 rounded-xl shadow-[var(--shadow-raised)] border border-border/80 bg-popover">
        <div className="px-2 py-1.5 text-[10px] font-semibold text-muted-foreground uppercase tracking-wider">
          AI Agent Profile
        </div>
        {profiles.map(profile => (
          <DropdownMenuItem
            key={profile.id}
            onSelect={() => handleSwitch(profile.id)}
            className="flex items-center justify-between rounded-lg px-2.5 py-1.5 text-xs cursor-pointer hover:bg-surface-hover transition-colors"
          >
            <span className={profile.id === activeProfileId ? "font-semibold text-primary" : "text-foreground"}>
              {profile.name}
            </span>
            {profile.id === activeProfileId && (
              <span className="size-1.5 rounded-full bg-primary" />
            )}
          </DropdownMenuItem>
        ))}
      </DropdownMenuContent>
    </DropdownMenu>
  );
}
