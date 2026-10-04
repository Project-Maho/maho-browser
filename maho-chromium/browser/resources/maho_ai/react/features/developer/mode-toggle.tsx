import * as React from 'react';
import { Tabs, TabsList, TabsTrigger } from '@ui/tabs';

export function ModeToggle(
    {
      developerMode,
      onSetDeveloperMode,
    }: {
      developerMode: boolean;
      onSetDeveloperMode: (enabled: boolean) => void;
    }) {
  return (
    <Tabs
      value={developerMode ? "developer" : "assistant"}
      onValueChange={(val: string) => onSetDeveloperMode(val === "developer")}
      className="w-full">
      <TabsList className="grid w-full grid-cols-2">
        <TabsTrigger value="assistant">Assistant</TabsTrigger>
        <TabsTrigger value="developer">Developer</TabsTrigger>
      </TabsList>
    </Tabs>
  );
}

