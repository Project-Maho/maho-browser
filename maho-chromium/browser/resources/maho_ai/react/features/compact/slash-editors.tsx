import React, {useState} from 'react';
import {Button} from '@ui/button';
import {Input} from '@ui/input';
import {User, Database, Code, X, Check, Settings} from '@icons/lucide';
import {useAppState} from '../../hooks/use-app-state.js';
import type {MahoAiStore} from '../../../store.js';

export function SlashEditors({store}: {store: MahoAiStore}) {
  const state = useAppState(store);
  const {activeSlashEditor, activeWorkspace} = state;

  if (!activeSlashEditor || !activeWorkspace) {
    return null;
  }

  const handleClose = () => {
    store.setActiveSlashEditor(null);
  };

  return (
    <div className="mx-3 my-2 p-4 rounded-2xl glass relative animate-in fade-in zoom-in-95 duration-200">
      <Button
        variant="ghost"
        size="icon"
        onClick={handleClose}
        className="absolute top-2 right-2 rounded-full size-6 text-muted-foreground hover:text-foreground hover:bg-surface-hover"
      >
        <X className="size-3.5" />
      </Button>

      {activeSlashEditor === 'agent-new' && (
        <AgentNewForm store={store} onClose={handleClose} />
      )}
      {activeSlashEditor === 'mcp-add' && (
        <McpAddForm store={store} workspaceId={activeWorkspace.id} onClose={handleClose} />
      )}
      {activeSlashEditor === 'cli-add' && (
        <CliAddForm store={store} workspaceId={activeWorkspace.id} onClose={handleClose} />
      )}
    </div>
  );
}

function AgentNewForm({store, onClose}: {store: MahoAiStore; onClose: () => void}) {
  const [name, setName] = useState('');
  const [prompt, setPrompt] = useState('');
  const [model, setModel] = useState('');
  const [loading, setLoading] = useState(false);

  const handleSave = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!name.trim()) return;
    setLoading(true);
    try {
      const success = await store.createAiProfile(
        name.trim(),
        prompt.trim(),
        model.trim() || null
      );
      if (success) {
        onClose();
      }
    } catch (err) {
      console.error(err);
    } finally {
      setLoading(false);
    }
  };

  return (
    <form onSubmit={handleSave} className="flex flex-col gap-3">
      <div className="flex items-center gap-2 text-primary font-semibold text-xs uppercase tracking-wider">
        <User className="size-4" />
        <span>Create AI Agent Profile</span>
      </div>
      <div className="flex flex-col gap-1.5">
        <label className="text-[10px] font-medium text-muted-foreground">Profile Name</label>
        <Input
          placeholder="e.g. Code Reviewer"
          value={name}
          onChange={e => setName(e.target.value)}
          required
          className="h-8 text-xs rounded-lg"
        />
      </div>
      <div className="flex flex-col gap-1.5">
        <label className="text-[10px] font-medium text-muted-foreground">System Instruction Prompt</label>
        <textarea
          placeholder="You are an expert developer..."
          value={prompt}
          onChange={e => setPrompt(e.target.value)}
          className="min-h-[80px] max-h-40 p-2 text-xs rounded-lg border border-input bg-background resize-y focus-visible:ring-1 focus-visible:ring-ring outline-none"
        />
      </div>
      <div className="flex flex-col gap-1.5">
        <label className="text-[10px] font-medium text-muted-foreground">Preferred Model (Optional)</label>
        <Input
          placeholder="gemini-2.5-pro"
          value={model}
          onChange={e => setModel(e.target.value)}
          className="h-8 text-xs rounded-lg"
        />
      </div>
      <div className="flex justify-end gap-2 mt-2">
        <Button type="button" variant="ghost" size="sm" onClick={onClose} className="h-8 text-xs rounded-lg">
          Cancel
        </Button>
        <Button type="submit" disabled={loading} size="sm" className="h-8 text-xs rounded-lg gap-1.5">
          <Check className="size-3.5" />
          <span>Save Profile</span>
        </Button>
      </div>
    </form>
  );
}

function McpAddForm({
  store,
  workspaceId,
  onClose,
}: {
  store: MahoAiStore;
  workspaceId: string;
  onClose: () => void;
}) {
  const [name, setName] = useState('');
  const [transport, setTransport] = useState<'stdio' | 'http'>('stdio');
  const [command, setCommand] = useState('');
  const [url, setUrl] = useState('');
  const [authKeychainId, setAuthKeychainId] = useState('');
  const [loading, setLoading] = useState(false);

  const handleSave = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!name.trim()) return;
    setLoading(true);
    try {
      const config = {
        id: '', // Will be generated in backend
        workspaceId,
        name: name.trim(),
        transport: transport === 'stdio' ? 0 : 1, // Mojom Enum
        command: transport === 'stdio' ? command.trim() : null,
        url: transport === 'http' ? url.trim() : null,
        authKeychainId: authKeychainId.trim() || null,
        trusted: false, // Must start untrusted
        trustedTools: null,
        timeoutMs: 10000,
        outputCapBytes: 1048576,
        createdAt: '',
        updatedAt: '',
      };
      const success = await store.registerMcpServer(workspaceId, JSON.stringify(config));
      if (success) {
        onClose();
      }
    } catch (err) {
      console.error(err);
    } finally {
      setLoading(false);
    }
  };

  return (
    <form onSubmit={handleSave} className="flex flex-col gap-3">
      <div className="flex items-center gap-2 text-primary font-semibold text-xs uppercase tracking-wider">
        <Database className="size-4" />
        <span>Add MCP Client Server</span>
      </div>
      <div className="flex flex-col gap-1.5">
        <label className="text-[10px] font-medium text-muted-foreground">Server Name</label>
        <Input
          placeholder="e.g. filesystem"
          value={name}
          onChange={e => setName(e.target.value)}
          required
          className="h-8 text-xs rounded-lg"
        />
      </div>
      <div className="flex flex-col gap-1.5">
        <label className="text-[10px] font-medium text-muted-foreground">Transport Protocol</label>
        <div className="flex gap-2">
          <Button
            type="button"
            variant={transport === 'stdio' ? 'default' : 'outline'}
            size="sm"
            onClick={() => setTransport('stdio')}
            className="flex-1 h-8 text-xs rounded-lg"
          >
            Stdio (Local Command)
          </Button>
          <Button
            type="button"
            variant={transport === 'http' ? 'default' : 'outline'}
            size="sm"
            onClick={() => setTransport('http')}
            className="flex-1 h-8 text-xs rounded-lg"
          >
            HTTP / SSE
          </Button>
        </div>
      </div>
      {transport === 'stdio' ? (
        <div className="flex flex-col gap-1.5">
          <label className="text-[10px] font-medium text-muted-foreground">Shell Command / JSON Array</label>
          <Input
            placeholder='npx -y @modelcontextprotocol/server-filesystem /path'
            value={command}
            onChange={e => setCommand(e.target.value)}
            required
            className="h-8 text-xs rounded-lg font-mono"
          />
        </div>
      ) : (
        <div className="flex flex-col gap-1.5">
          <label className="text-[10px] font-medium text-muted-foreground">SSE Server URL</label>
          <Input
            placeholder="http://localhost:3001/sse"
            value={url}
            onChange={e => setUrl(e.target.value)}
            required
            className="h-8 text-xs rounded-lg font-mono"
          />
        </div>
      )}
      <div className="flex flex-col gap-1.5">
        <label className="text-[10px] font-medium text-muted-foreground">Auth Keychain ID (Optional)</label>
        <Input
          placeholder="e.g. github_token"
          value={authKeychainId}
          onChange={e => setAuthKeychainId(e.target.value)}
          className="h-8 text-xs rounded-lg"
        />
      </div>
      <div className="flex justify-end gap-2 mt-2">
        <Button type="button" variant="ghost" size="sm" onClick={onClose} className="h-8 text-xs rounded-lg">
          Cancel
        </Button>
        <Button type="submit" disabled={loading} size="sm" className="h-8 text-xs rounded-lg gap-1.5">
          <Check className="size-3.5" />
          <span>Add Server</span>
        </Button>
      </div>
    </form>
  );
}

function CliAddForm({
  store,
  workspaceId,
  onClose,
}: {
  store: MahoAiStore;
  workspaceId: string;
  onClose: () => void;
}) {
  const [name, setName] = useState('');
  const [description, setDescription] = useState('');
  const [commandTemplate, setCommandTemplate] = useState('');
  const [workingDir, setWorkingDir] = useState('');
  const [loading, setLoading] = useState(false);

  const handleSave = async (e: React.FormEvent) => {
    e.preventDefault();
    if (!name.trim() || !commandTemplate.trim()) return;
    setLoading(true);
    try {
      const tool = {
        id: '', // Will be generated in backend
        workspaceId,
        name: name.trim(),
        description: description.trim(),
        commandTemplate: commandTemplate.trim(),
        parametersSchema: {
          type: 'object',
          properties: {},
          required: [],
        },
        sensitive: true, // Always sensitive by default
        timeoutMs: 10000,
        outputCapBytes: 1048576,
        workingDirectory: workingDir.trim() || null,
        createdAt: '',
        updatedAt: '',
      };
      const success = await store.registerCliTool(workspaceId, JSON.stringify(tool));
      if (success) {
        onClose();
      }
    } catch (err) {
      console.error(err);
    } finally {
      setLoading(false);
    }
  };

  return (
    <form onSubmit={handleSave} className="flex flex-col gap-3">
      <div className="flex items-center gap-2 text-primary font-semibold text-xs uppercase tracking-wider">
        <Code className="size-4" />
        <span>Add CLI Tool</span>
      </div>
      <div className="flex flex-col gap-1.5">
        <label className="text-[10px] font-medium text-muted-foreground">Tool Name</label>
        <Input
          placeholder="e.g. git_diff"
          value={name}
          onChange={e => setName(e.target.value)}
          required
          className="h-8 text-xs rounded-lg"
        />
      </div>
      <div className="flex flex-col gap-1.5">
        <label className="text-[10px] font-medium text-muted-foreground">Description (for LLM discovery)</label>
        <Input
          placeholder="Retrieve git diff in the current workspace"
          value={description}
          onChange={e => setDescription(e.target.value)}
          required
          className="h-8 text-xs rounded-lg"
        />
      </div>
      <div className="flex flex-col gap-1.5">
        <label className="text-[10px] font-medium text-muted-foreground">Command Template</label>
        <Input
          placeholder="git diff HEAD"
          value={commandTemplate}
          onChange={e => setCommandTemplate(e.target.value)}
          required
          className="h-8 text-xs rounded-lg font-mono"
        />
      </div>
      <div className="flex flex-col gap-1.5">
        <label className="text-[10px] font-medium text-muted-foreground">Working Directory (Optional)</label>
        <Input
          placeholder="/Users/example/project"
          value={workingDir}
          onChange={e => setWorkingDir(e.target.value)}
          className="h-8 text-xs rounded-lg"
        />
      </div>
      <div className="flex justify-end gap-2 mt-2">
        <Button type="button" variant="ghost" size="sm" onClick={onClose} className="h-8 text-xs rounded-lg">
          Cancel
        </Button>
        <Button type="submit" disabled={loading} size="sm" className="h-8 text-xs rounded-lg gap-1.5">
          <Check className="size-3.5" />
          <span>Add Tool</span>
        </Button>
      </div>
    </form>
  );
}
