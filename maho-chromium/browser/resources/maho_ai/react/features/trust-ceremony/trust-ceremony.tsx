import React from 'react';
import {Button} from '@ui/button';
import {ShieldAlert, Check, X, Key, Sliders} from '@icons/lucide';
import {useAppState} from '../../hooks/use-app-state.js';
import {MahoAiStore} from '../../../store.js';

export function TrustCeremony({store}: {store: MahoAiStore}) {
  const state = useAppState(store);
  const {mcpServers, activeWorkspace} = state;
  const [showAdvanced, setShowAdvanced] = React.useState(false);
  const [customTools, setCustomTools] = React.useState('');
  const [submitting, setSubmitting] = React.useState(false);

  if (!activeWorkspace) {
    return null;
  }

  const untrustedServer = mcpServers.find(
    s => s.workspaceId === activeWorkspace.id && !s.trusted
  );

  if (!untrustedServer) {
    return null;
  }

  const handleApproveAll = async () => {
    setSubmitting(true);
    try {
      await store.approveMcpServerTrust(activeWorkspace.id, untrustedServer.name, []);
    } finally {
      setSubmitting(false);
    }
  };

  const handleApproveCustom = async () => {
    const tools = customTools
      .split(',')
      .map(t => t.trim())
      .filter(t => t.length > 0);
    if (tools.length === 0) {
      return;
    }
    setSubmitting(true);
    try {
      const trusted = await store.approveMcpServerTrust(
          activeWorkspace.id, untrustedServer.name, tools);
      if (trusted) {
        setCustomTools('');
        setShowAdvanced(false);
      }
    } finally {
      setSubmitting(false);
    }
  };

  const handleDeny = async () => {
    setSubmitting(true);
    try {
      await store.removeMcpServer(activeWorkspace.id, untrustedServer.name);
    } finally {
      setSubmitting(false);
    }
  };

  return (
    <div className="m-3 p-4 rounded-2xl border border-warning/30 bg-warning/5 shadow-[var(--shadow-raised)] animate-in fade-in slide-in-from-top-4 duration-300">
      <div className="flex items-start gap-3">
        <div className="p-2 rounded-xl bg-warning/20 text-warning">
          <ShieldAlert className="size-5" />
        </div>
        <div className="flex-1 min-w-0">
          <h4 className="text-sm font-semibold text-foreground flex items-center gap-1.5">
            Security: Trust MCP Server?
          </h4>
          <p className="mt-1 text-xs text-muted-foreground leading-normal">
            MCP Server <code className="bg-muted px-1 py-0.5 rounded text-[11px] font-mono">{untrustedServer.name}</code> is requesting integration.
            Trusting grants this server permission to run its tools without a per-call prompt.
            Sensitive tools always prompt regardless.
          </p>

          <div className="mt-3 flex items-center gap-1.5 bg-muted/50 border border-border/40 px-2.5 py-1.5 rounded-lg text-[11px] font-mono text-muted-foreground w-fit">
            <Key className="size-3.5 text-warning/80" />
            <span>Keychain ID:</span>
            <span className="font-semibold text-foreground select-all">
              {untrustedServer.authKeychainId || 'none (default)'}
            </span>
          </div>

          <div className="mt-4 flex items-center gap-2 flex-wrap">
            <Button
              size="sm"
              variant="default"
              onClick={handleApproveAll}
              disabled={submitting}
              className="flex items-center gap-1 h-8 px-3 rounded-lg text-xs font-medium bg-warning text-warning-foreground hover:bg-warning/90"
            >
              <Check className="size-3.5" />
              <span>Trust All Tools</span>
            </Button>
            <Button
              size="sm"
              variant="outline"
              onClick={() => setShowAdvanced(v => !v)}
              disabled={submitting}
              className="flex items-center gap-1 h-8 px-3 rounded-lg text-xs font-medium border-border/60"
            >
              <Sliders className="size-3.5" />
              <span>{showAdvanced ? 'Hide' : 'Customize'}</span>
            </Button>
            <Button
              size="sm"
              variant="outline"
              onClick={handleDeny}
              disabled={submitting}
              className="flex items-center gap-1 h-8 px-3 rounded-lg text-xs font-medium border-border/60 hover:bg-destructive/10 hover:text-destructive hover:border-destructive/30"
            >
              <X className="size-3.5" />
              <span>Deny / Block</span>
            </Button>
          </div>

          {showAdvanced ? (
            <div className="mt-3 p-3 rounded-lg border border-border/40 bg-background/50 space-y-2">
              <label className="text-[11px] font-medium text-foreground block">
                Trust only these tools <span className="text-muted-foreground">(comma-separated names)</span>
                <input
                  type="text"
                  value={customTools}
                  onChange={e => setCustomTools(e.target.value)}
                  placeholder="e.g. read_file, list_directory"
                  disabled={submitting}
                  className="mt-1 w-full h-8 px-2 rounded-md border border-border/60 bg-background text-xs font-mono focus:outline-none focus:ring-1 focus:ring-warning/50"
                />
              </label>
              <div className="flex items-center gap-2">
                <Button
                  size="sm"
                  variant="default"
                  onClick={handleApproveCustom}
                  disabled={submitting || customTools.trim().length === 0}
                  className="h-7 px-3 rounded-md text-xs"
                >
                  Approve Selected
                </Button>
                <span className="text-[10px] text-muted-foreground">
                  Tools not in this list will always prompt.
                </span>
              </div>
            </div>
          ) : null}
        </div>
      </div>
    </div>
  );
}
