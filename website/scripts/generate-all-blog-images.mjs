import { Resvg } from '@resvg/resvg-js';
import { html } from 'satori-html';
import satori from 'satori';
import { createRequire } from 'node:module';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { readdirSync, readFileSync, writeFileSync, mkdirSync } from 'node:fs';

const __dirname = dirname(fileURLToPath(import.meta.url));
const require = createRequire(import.meta.url);
const projectRoot = join(__dirname, '..');
const blogAssetsRoot = join(projectRoot, 'public', 'assets', 'blog');

function resolveFontFile(pkg, matcher) {
  const dir = join(dirname(require.resolve(`${pkg}/package.json`)), 'files');
  const m = readdirSync(dir).find((f) => matcher.test(f));
  if (!m) throw new Error(`no font in ${dir} for ${matcher}`);
  return join(dir, m);
}

const fonts = [
  { name: 'Instrument Serif', data: readFileSync(resolveFontFile('@fontsource/instrument-serif', /instrument-serif-latin-400-normal\.woff$/)), weight: 400, style: 'normal' },
  { name: 'Geist', data: readFileSync(resolveFontFile('@fontsource/geist', /geist-latin-400-normal\.woff$/)), weight: 400, style: 'normal' },
  { name: 'Geist', data: readFileSync(resolveFontFile('@fontsource/geist', /geist-latin-500-normal\.woff$/)), weight: 500, style: 'normal' },
  { name: 'Geist Mono', data: readFileSync(resolveFontFile('@fontsource/geist-mono', /geist-mono-latin-400-normal\.woff$/)), weight: 400, style: 'normal' },
];

const W = 1376;
const H = 768;
const ACCENT = '#8b7ff5';
const CYAN = '#5fb3c7';

function renderHeroCard({ tag, protocol, title, subtitle, boxTitle, boxTag, items, bottomStatus }) {
  const itemsHtml = items.map(item => `
    <div style="display: flex; justify-content: space-between; align-items: center; background: rgba(255,255,255,0.04); padding: 10px 14px; border-radius: 8px; border: 1px solid rgba(255,255,255,0.07); margin-bottom: 8px;">
      <span style="font-family: 'Geist Mono'; font-size: 13px; color: ${item.color || '#fafafa'};">${item.label}</span>
      <span style="font-family: 'Geist Mono'; font-size: 12px; color: ${item.statusColor || '#a1a1aa'};">${item.value}</span>
    </div>
  `).join('');

  return `
  <div style="display: flex; flex-direction: column; width: ${W}px; height: ${H}px; background: #0a0a0a; position: relative; padding: 64px 80px; box-sizing: border-box; justify-content: space-between;">
    <div style="display: flex; position: absolute; top: -100px; right: -100px; width: 600px; height: 600px; background: radial-gradient(circle, rgba(139,127,245,0.18) 0%, transparent 70%);"></div>
    <div style="display: flex; position: absolute; bottom: -80px; left: -80px; width: 500px; height: 500px; background: radial-gradient(circle, rgba(95,179,199,0.12) 0%, transparent 70%);"></div>

    <!-- Top Bar -->
    <div style="display: flex; justify-content: space-between; align-items: center; width: 100%;">
      <div style="display: flex; align-items: center; gap: 12px;">
        <div style="display: flex; width: 10px; height: 10px; border-radius: 5px; background: ${ACCENT};"></div>
        <span style="font-family: 'Geist Mono'; font-size: 17px; letter-spacing: 0.16em; text-transform: uppercase; color: ${ACCENT};">${tag}</span>
      </div>
      <div style="display: flex; align-items: center; gap: 8px; background: rgba(255,255,255,0.06); padding: 6px 16px; border-radius: 20px; border: 1px solid rgba(255,255,255,0.1);">
        <span style="font-family: 'Geist Mono'; font-size: 14px; color: #a1a1aa;">${protocol}</span>
      </div>
    </div>

    <!-- Center Content -->
    <div style="display: flex; justify-content: space-between; align-items: center; gap: 48px; width: 100%;">
      <div style="display: flex; flex-direction: column; max-width: 620px;">
        <div style="display: flex; font-family: 'Instrument Serif'; font-size: 62px; line-height: 1.05; letter-spacing: -0.02em; color: #fafafa;">
          ${title}
        </div>
        <div style="display: flex; font-family: 'Geist'; font-size: 21px; line-height: 1.45; color: #a1a1aa; margin-top: 20px;">
          ${subtitle}
        </div>
      </div>

      <div style="display: flex; flex-direction: column; width: 480px; background: rgba(18,18,20,0.85); border: 1px solid rgba(255,255,255,0.12); border-radius: 16px; padding: 24px; box-shadow: 0 20px 50px rgba(0,0,0,0.5);">
        <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 16px; padding-bottom: 12px; border-bottom: 1px solid rgba(255,255,255,0.08);">
          <span style="font-family: 'Geist'; font-weight: 500; font-size: 17px; color: #fafafa;">${boxTitle}</span>
          <span style="font-family: 'Geist Mono'; font-size: 13px; color: ${ACCENT};">${boxTag}</span>
        </div>
        <div style="display: flex; flex-direction: column;">
          ${itemsHtml}
        </div>
        <div style="display: flex; justify-content: space-between; align-items: center; margin-top: 12px; font-family: 'Geist Mono'; font-size: 12px; color: #71717a;">
          <span>Status</span>
          <span style="color: #34d399;">${bottomStatus || 'Active / Ready'}</span>
        </div>
      </div>
    </div>

    <!-- Bottom Bar -->
    <div style="display: flex; justify-content: space-between; align-items: center; width: 100%; border-top: 1px solid rgba(255,255,255,0.08); padding-top: 20px;">
      <span style="font-family: 'Geist Mono'; font-size: 15px; letter-spacing: 0.14em; color: #fafafa;">MAHO BROWSER</span>
      <span style="font-family: 'Geist Mono'; font-size: 14px; color: #71717a;">mahobrowser.com/blog</span>
    </div>
  </div>`;
}

function renderDiagramCard({ tag, title, subtitle, columns }) {
  const colsHtml = columns.map(col => `
    <div style="display: flex; flex-direction: column; flex: 1; background: rgba(18,18,20,0.85); border: 1px solid ${col.highlight ? 'rgba(139,127,245,0.4)' : 'rgba(255,255,255,0.1)'}; border-radius: 14px; padding: 20px; box-shadow: 0 15px 35px rgba(0,0,0,0.4);">
      <div style="display: flex; justify-content: space-between; align-items: center; margin-bottom: 14px;">
        <span style="font-family: 'Geist'; font-weight: 500; font-size: 18px; color: ${col.highlight ? '#fafafa' : '#e4e4e7'};">${col.header}</span>
        <span style="font-family: 'Geist Mono'; font-size: 12px; color: ${col.highlight ? ACCENT : '#71717a'};">${col.badge || ''}</span>
      </div>
      <div style="display: flex; flex-direction: column; gap: 8px;">
        ${col.rows.map(r => `
          <div style="display: flex; flex-direction: column; background: rgba(255,255,255,0.03); padding: 8px 12px; border-radius: 6px; border: 1px solid rgba(255,255,255,0.05);">
            <span style="font-family: 'Geist Mono'; font-size: 12px; color: ${r.color || '#a1a1aa'};">${r.k}</span>
            <span style="font-family: 'Geist'; font-size: 14px; color: #fafafa; margin-top: 2px;">${r.v}</span>
          </div>
        `).join('')}
      </div>
    </div>
  `).join('');

  return `
  <div style="display: flex; flex-direction: column; width: ${W}px; height: ${H}px; background: #0a0a0a; position: relative; padding: 56px 72px; box-sizing: border-box; justify-content: space-between;">
    <div style="display: flex; position: absolute; top: -80px; left: 30%; width: 600px; height: 500px; background: radial-gradient(circle, rgba(139,127,245,0.12) 0%, transparent 70%);"></div>

    <!-- Header -->
    <div style="display: flex; justify-content: space-between; align-items: center; width: 100%;">
      <div style="display: flex; flex-direction: column;">
        <span style="font-family: 'Geist Mono'; font-size: 14px; letter-spacing: 0.16em; text-transform: uppercase; color: ${ACCENT};">${tag}</span>
        <span style="font-family: 'Instrument Serif'; font-size: 38px; color: #fafafa; margin-top: 4px;">${title}</span>
      </div>
      <span style="font-family: 'Geist'; font-size: 16px; color: #a1a1aa; max-width: 440px; text-align: right;">${subtitle}</span>
    </div>

    <!-- Columns -->
    <div style="display: flex; gap: 20px; width: 100%; margin-top: 20px; margin-bottom: 20px;">
      ${colsHtml}
    </div>

    <!-- Footer -->
    <div style="display: flex; justify-content: space-between; align-items: center; width: 100%; border-top: 1px solid rgba(255,255,255,0.08); padding-top: 16px;">
      <span style="font-family: 'Geist Mono'; font-size: 14px; color: #71717a;">Maho Architectural Blueprint</span>
      <span style="font-family: 'Geist Mono'; font-size: 14px; color: ${ACCENT};">mahobrowser.com</span>
    </div>
  </div>`;
}

async function generatePng(markupHtml, outFilePath) {
  const svg = await satori(html(markupHtml), { width: W, height: H, fonts });
  const png = new Resvg(svg, { fitTo: { mode: 'width', value: W } }).render().asPng();
  mkdirSync(dirname(outFilePath), { recursive: true });
  writeFileSync(outFilePath, png);
  console.log(`Generated: ${outFilePath} (${png.length} bytes)`);
}

// 1. connect-claude-desktop-cursor-to-maho-via-mcp
const mcpSlug = 'connect-claude-desktop-cursor-to-maho-via-mcp';
await generatePng(
  renderHeroCard({
    tag: 'INTEGRATION · MCP BRIDGE',
    protocol: 'stdio JSON-RPC 2.0',
    title: 'Connect Claude & Cursor to Maho via MCP',
    subtitle: 'Expose live browser tabs, page markdown extraction, and email tools to external AI hosts with zero configuration.',
    boxTitle: 'Maho MCP stdio Bridge',
    boxTag: 'Active Protocol',
    items: [
      { label: 'Client', value: 'Claude Desktop / Cursor', color: '#fafafa' },
      { label: 'Transport', value: 'stdio → Unix Domain Socket', color: '#8b7ff5' },
      { label: 'Security', value: '3-Gate Permission Enforcement', color: '#5fb3c7' },
      { label: 'Tool Pool', value: 'browser_tabs · page_text · mail_*', color: '#fafafa' },
    ],
    bottomStatus: 'Authenticated & Bound'
  }),
  join(blogAssetsRoot, mcpSlug, 'hero.png')
);

await generatePng(
  renderDiagramCard({
    tag: 'SYSTEM TOPOLOGY',
    title: 'Maho Browser MCP Bridge Flow',
    subtitle: 'From AI desktop client stdio to native Chromium browser core via local domain socket.',
    columns: [
      {
        header: '1. AI MCP Host',
        badge: 'Claude / Cursor',
        rows: [
          { k: 'Interface', v: 'claude_desktop_config.json' },
          { k: 'Execution', v: 'maho mcp bridge process' },
          { k: 'Payload', v: 'JSON-RPC tools/call requests' }
        ]
      },
      {
        header: '2. Stdio Bridge',
        badge: 'maho-browser-mcp',
        highlight: true,
        rows: [
          { k: 'Auth', v: 'Local UDS Token Handshake' },
          { k: 'Security', v: 'Mutation Approval Broker' },
          { k: 'Latency', v: '< 5ms in-process dispatch' }
        ]
      },
      {
        header: '3. Maho Core',
        badge: 'Chromium Overlay',
        rows: [
          { k: 'Surfaces', v: 'Tab Lifecycle & DOM Tree' },
          { k: 'Extraction', v: 'FTS5 Indexed Local DB' },
          { k: 'Protection', v: 'User Read/Write Consent' }
        ]
      }
    ]
  }),
  join(blogAssetsRoot, mcpSlug, 'body-1.png')
);

await generatePng(
  renderDiagramCard({
    tag: 'SECURITY ARCHITECTURE',
    title: '3-Gate Tool Authorization Model',
    subtitle: 'How sensitive browser actions and email reads are protected against unauthorized tool calls.',
    columns: [
      {
        header: 'Gate 1: Feature Enable',
        badge: 'Profile Flag',
        rows: [
          { k: 'Check', v: 'maho.mail.enabled == true' },
          { k: 'State', v: 'Explicit user opt-in in Settings' },
          { k: 'Failure', v: 'mail_feature_disabled (-32008)' }
        ]
      },
      {
        header: 'Gate 2: Helper Ready',
        badge: 'Lifecycle',
        rows: [
          { k: 'Check', v: 'Helper IPC socket ready' },
          { k: 'State', v: 'Sandboxed background child' },
          { k: 'Failure', v: 'mail_helper_unavailable' }
        ]
      },
      {
        header: 'Gate 3: Read Consent',
        badge: 'Strict Privacy',
        highlight: true,
        rows: [
          { k: 'Check', v: 'Allow AI to read Mail == true' },
          { k: 'State', v: 'Off by default; explicit toggle' },
          { k: 'Failure', v: 'mail_read_consent_required' }
        ]
      }
    ]
  }),
  join(blogAssetsRoot, mcpSlug, 'body-2.png')
);

// 2. terminal-browser-control-with-maho-cli
const cliSlug = 'terminal-browser-control-with-maho-cli';
await generatePng(
  renderHeroCard({
    tag: 'DEVELOPER GUIDE · CLI',
    protocol: 'Unix Pipes & JSON-first',
    title: 'Control Browser from the Terminal with Maho',
    subtitle: 'Query history, manage live tabs, extract rendered DOMs to Markdown, and run autonomous agent sessions from your shell.',
    boxTitle: 'Maho CLI Command Surface',
    boxTag: 'v2026.8',
    items: [
      { label: 'Tabs', value: 'maho tab [list|open|eval]', color: '#8b7ff5' },
      { label: 'Extract', value: 'maho page markdown | llm', color: '#5fb3c7' },
      { label: 'History', value: 'maho history search "query"', color: '#fafafa' },
      { label: 'Autonomous', value: 'maho run "summarize page"', color: '#fafafa' },
    ],
    bottomStatus: 'Local Socket Connected'
  }),
  join(blogAssetsRoot, cliSlug, 'hero.png')
);

await generatePng(
  renderDiagramCard({
    tag: 'UNIX COMPOSABILITY',
    title: 'Terminal Pipeline & Data Flow',
    subtitle: 'Seamlessly piping rendered browser output through standard Unix command line tools.',
    columns: [
      {
        header: '1. Browser Extract',
        badge: 'maho CLI',
        rows: [
          { k: 'Command', v: 'maho page markdown --json' },
          { k: 'Source', v: 'Active Chromium DOM Tree' },
          { k: 'Output', v: 'Structured JSON / Clean MD' }
        ]
      },
      {
        header: '2. Filter & Transform',
        badge: 'Unix Utilities',
        highlight: true,
        rows: [
          { k: 'Piping', v: 'stdout -> jq / fzf / rg' },
          { k: 'Processing', v: 'Strip boilerplate & ads' },
          { k: 'Formatting', v: 'LLM context payload' }
        ]
      },
      {
        header: '3. AI Ingestion',
        badge: 'Agent / Model',
        rows: [
          { k: 'Destination', v: 'llm / local Ollama / Claude' },
          { k: 'Execution', v: 'Autonomous Action Loop' },
          { k: 'Result', v: 'Synthesized Insight / Ticket' }
        ]
      }
    ]
  }),
  join(blogAssetsRoot, cliSlug, 'body-1.png')
);

await generatePng(
  renderDiagramCard({
    tag: 'AGENT RUNTIME',
    title: 'Autonomous CLI Agent Execution',
    subtitle: 'How `maho run` coordinates model reasoning and browser tool execution without UI overhead.',
    columns: [
      {
        header: 'Prompt & Plan',
        badge: 'Shell Input',
        rows: [
          { k: 'Input', v: 'maho run --provider anthropic' },
          { k: 'Routine', v: 'Optional JSON Workflow' },
          { k: 'Model', v: 'Claude 3.7 / Local BYOK' }
        ]
      },
      {
        header: 'Tool Calling Loop',
        badge: 'maho-agent',
        highlight: true,
        rows: [
          { k: 'Inspection', v: 'browser_get_dom / tabs' },
          { k: 'Interaction', v: 'click / type / scroll / nav' },
          { k: 'Verification', v: 'Step-by-step state check' }
        ]
      },
      {
        header: 'Artifact & Log',
        badge: 'Output Store',
        rows: [
          { k: 'Persistence', v: 'SQLite session database' },
          { k: 'Artifacts', v: 'Screenshots & Markdown files' },
          { k: 'Terminal', v: 'Clean streaming text / JSON' }
        ]
      }
    ]
  }),
  join(blogAssetsRoot, cliSlug, 'body-2.png')
);

// 3. Comparison Posts body-1.png generation
const compSlugs = [
  {
    slug: 'agentic-browser-comparison-2026',
    tag: 'MARKET COMPARISON',
    title: 'Agentic Browser Architectures (2026)',
    subtitle: 'Local-first native browser control vs cloud-hosted VM automation models.',
    columns: [
      {
        header: 'Maho Browser',
        badge: 'Local Native',
        highlight: true,
        rows: [
          { k: 'Execution', v: 'On-device Chromium Core' },
          { k: 'Privacy', v: 'Local SQLite, BYOK API Keys' },
          { k: 'Speed', v: 'Zero VM latency, native DOM' }
        ]
      },
      {
        header: 'Cloud Agentic (Operator/MultiOn)',
        badge: 'Remote Cloud',
        rows: [
          { k: 'Execution', v: 'Remote Cloud Linux VM' },
          { k: 'Privacy', v: 'Credentials stored on server' },
          { k: 'Speed', v: 'Streaming video latency' }
        ]
      },
      {
        header: 'Chat Assistants (Brave/Copilot)',
        badge: 'Sidebar Wrapper',
        rows: [
          { k: 'Execution', v: 'Sidebar iframe chat' },
          { k: 'Capabilities', v: 'Read-only page summaries' },
          { k: 'Action', v: 'No autonomous tool calling' }
        ]
      }
    ]
  },
  {
    slug: 'maho-vs-aside-browser',
    tag: 'PHILOSOPHY COMPARISON',
    title: 'Maho vs Aside: Two Agentic Approaches',
    subtitle: 'Typed browser capabilities vs full OS desktop computer use automation.',
    columns: [
      {
        header: 'Maho Browser',
        badge: 'Browser Focused',
        highlight: true,
        rows: [
          { k: 'Control Level', v: 'Direct DOM & Tab APIs' },
          { k: 'Reliability', v: 'Deterministic tool-calling' },
          { k: 'Safety', v: 'Scoped to browser window' }
        ]
      },
      {
        header: 'Aside Browser',
        badge: 'Full OS Agent',
        rows: [
          { k: 'Control Level', v: 'Global OS Accessibility & Mouse' },
          { k: 'Reliability', v: 'Vision-based pixel clicking' },
          { k: 'Safety', v: 'Full desktop screen permissions' }
        ]
      }
    ]
  },
  {
    slug: 'maho-mail-vs-himalaya',
    tag: 'TOOL COMPARISON',
    title: 'Maho Mail vs Himalaya CLI',
    subtitle: 'Integrated multi-surface client vs standalone terminal email binary.',
    columns: [
      {
        header: 'Maho Mail',
        badge: 'Hybrid Platform',
        highlight: true,
        rows: [
          { k: 'Surfaces', v: 'Desktop GUI + CLI + MCP' },
          { k: 'Database', v: 'Shared encrypted SQLite' },
          { k: 'AI Tools', v: 'Native stdio MCP bridge' }
        ]
      },
      {
        header: 'Himalaya',
        badge: 'CLI Only',
        rows: [
          { k: 'Surfaces', v: 'Terminal command line only' },
          { k: 'Database', v: 'Direct IMAP/Maildir queries' },
          { k: 'AI Tools', v: 'Requires external scripts' }
        ]
      }
    ]
  },
  {
    slug: 'maho-mail-vs-spark',
    tag: 'COLLABORATION VS PRIVACY',
    title: 'Maho Mail vs Spark Email',
    subtitle: 'Solo programmable automation vs team collaboration cloud services.',
    columns: [
      {
        header: 'Maho Mail',
        badge: 'Local-First',
        highlight: true,
        rows: [
          { k: 'Storage', v: '100% on-device SQLCipher' },
          { k: 'Automation', v: 'Terminal pipes & AI agents' },
          { k: 'Pricing', v: 'Free local tier / BYOK AI' }
        ]
      },
      {
        header: 'Spark Email',
        badge: 'Cloud Sync',
        rows: [
          { k: 'Storage', v: 'Processed on Spark servers' },
          { k: 'Automation', v: 'GUI rule-based smart inbox' },
          { k: 'Pricing', v: 'Monthly cloud subscription' }
        ]
      }
    ]
  },
  {
    slug: 'maho-mail-vs-superhuman',
    tag: 'SPEED & AUTOMATION',
    title: 'Maho Mail vs Superhuman',
    subtitle: 'Programmable developer interface vs closed keyboard GUI client.',
    columns: [
      {
        header: 'Maho Mail',
        badge: 'Programmable',
        highlight: true,
        rows: [
          { k: 'Data Access', v: 'Structured JSON via CLI/MCP' },
          { k: 'AI Model', v: 'Any model provider (BYOK)' },
          { k: 'Platform', v: 'Open local SQLite database' }
        ]
      },
      {
        header: 'Superhuman',
        badge: 'Closed GUI',
        rows: [
          { k: 'Data Access', v: 'UI shortcuts & split inbox' },
          { k: 'AI Model', v: 'Proprietary built-in AI' },
          { k: 'Platform', v: 'Closed cloud ecosystem' }
        ]
      }
    ]
  },
  {
    slug: 'maho-mail-vs-thunderbird',
    tag: 'LEGACY VS AGENTIC',
    title: 'Maho Mail vs Mozilla Thunderbird',
    subtitle: 'Modern AI-broker architecture vs traditional 2000s desktop PIM.',
    columns: [
      {
        header: 'Maho Mail',
        badge: 'Agentic Native',
        highlight: true,
        rows: [
          { k: 'Architecture', v: 'In-process MCP agent broker' },
          { k: 'Search', v: 'SQLite FTS5 full-text search' },
          { k: 'Design', v: 'Minimal keyboard-first UI' }
        ]
      },
      {
        header: 'Thunderbird',
        badge: 'Classic PIM',
        rows: [
          { k: 'Architecture', v: 'Legacy Gecko engine & add-ons' },
          { k: 'Search', v: 'Local folder indexer' },
          { k: 'Design', v: 'Heavy multi-pane PIM suite' }
        ]
      }
    ]
  },
  {
    slug: 'maho-mail-vs-shortwave',
    tag: 'ARCHITECTURE COMPARISON',
    title: 'Maho Mail vs Shortwave',
    subtitle: 'Local-first multi-interface vs cloud Gmail AI wrapper.',
    columns: [
      {
        header: 'Maho Mail',
        badge: 'Local First',
        highlight: true,
        rows: [
          { k: 'Providers', v: 'Gmail, Fastmail, IMAP/SMTP' },
          { k: 'Access', v: 'Browser + Shell + MCP server' },
          { k: 'Privacy', v: 'Zero remote tracking / BYOK' }
        ]
      },
      {
        header: 'Shortwave',
        badge: 'Cloud Web App',
        rows: [
          { k: 'Providers', v: 'Gmail accounts primarily' },
          { k: 'Access', v: 'Web & mobile app only' },
          { k: 'Privacy', v: 'Cloud AI processing pipeline' }
        ]
      }
    ]
  },
  {
    slug: 'maho-mail-vs-apple-mail',
    tag: 'PLATFORM COMPARISON',
    title: 'Maho Mail vs Apple Mail',
    subtitle: 'Developer-oriented agentic mail vs macOS system default client.',
    columns: [
      {
        header: 'Maho Mail',
        badge: 'Developer Native',
        highlight: true,
        rows: [
          { k: 'CLI & API', v: 'JSON-first CLI & MCP stdio' },
          { k: 'Search', v: 'Instant FTS5 database search' },
          { k: 'Automation', v: 'Programmable AI tool calling' }
        ]
      },
      {
        header: 'Apple Mail',
        badge: 'OS Default',
        rows: [
          { k: 'CLI & API', v: 'AppleScript / MailKit only' },
          { k: 'Search', v: 'Spotlight system indexing' },
          { k: 'Automation', v: 'Apple Intelligence summaries' }
        ]
      }
    ]
  },
  {
    slug: 'maho-mail-vs-aerc-mutt-neomutt',
    tag: 'TUI COMPARISON',
    title: 'Maho Mail vs aerc / mutt / neomutt',
    subtitle: 'Integrated multi-surface client vs pure terminal TUI utilities.',
    columns: [
      {
        header: 'Maho Mail',
        badge: 'Multi-Surface',
        highlight: true,
        rows: [
          { k: 'Interface', v: 'Fast WebUI + Terminal CLI' },
          { k: 'AI Agents', v: 'Built-in MCP tool interface' },
          { k: 'Rendering', v: 'Full HTML email sandbox' }
        ]
      },
      {
        header: 'aerc / mutt / neomutt',
        badge: 'TUI Only',
        rows: [
          { k: 'Interface', v: 'Ncurses terminal curses UI' },
          { k: 'AI Agents', v: 'Manual bash pipe wrappers' },
          { k: 'Rendering', v: 'w3m / lynx HTML conversion' }
        ]
      }
    ]
  },
  {
    slug: 'verify-maho-locally',
    tag: 'SECURITY VERIFICATION',
    title: 'Verifying Maho Privacy Locally',
    subtitle: 'How to audit network traffic, encryption keys, and SQLite records on your machine.',
    columns: [
      {
        header: '1. Network Audit',
        badge: 'Wireshark / lsof',
        rows: [
          { k: 'Command', v: 'lsof -i -P | grep Maho' },
          { k: 'Check', v: 'No telemetry beacons transmitted' },
          { k: 'Target', v: 'Only user-selected AI endpoints' }
        ]
      },
      {
        header: '2. Storage Audit',
        badge: 'SQLite / SQLCipher',
        highlight: true,
        rows: [
          { k: 'Database', v: 'history.sqlite / mail.sqlite' },
          { k: 'Encryption', v: 'SQLCipher AES-256-GCM' },
          { k: 'Keys', v: 'Stored in macOS Keychain' }
        ]
      },
      {
        header: '3. IPC Socket Audit',
        badge: 'UDS Permissions',
        rows: [
          { k: 'Socket', v: '/tmp/maho-$USER.sock' },
          { k: 'Access', v: '0700 file permission enforcement' },
          { k: 'Audit', v: 'Inspect live stdio bridge' }
        ]
      }
    ]
  }
];

for (const comp of compSlugs) {
  await generatePng(
    renderDiagramCard(comp),
    join(blogAssetsRoot, comp.slug, 'body-1.png')
  );
}

// 4. Maho vs Arc/Zen UX series (spaces / split view / agentic hour)
const arcZenPosts = [
  {
    slug: 'maho-spaces-tidy-sidebar',
    hero: {
      tag: 'UX SERIES · SPACES',
      protocol: 'Option+1 / Option+2 / Option+3',
      title: 'Spaces that stay tidy',
      subtitle: 'Distinct contexts without hidden furniture: every open tab stays visible while spaces keep work, writing, and personal browsing apart.',
      boxTitle: 'Sidebar Tab Ledger',
      boxTag: 'Source of Truth',
      items: [
        { label: 'Open tabs', value: 'All listed in sidebar' },
        { label: 'Silent moves', value: 'None — nothing hides' },
        { label: 'Space switching', value: 'Instant, per-context' },
        { label: 'Orphan tabs', value: 'Reconciled automatically' },
      ],
      bottomStatus: 'Visible & Trustworthy'
    },
    body1: {
      tag: 'ARCHIVE VS LEDGER',
      title: 'Auto-archive can bury the tab you need',
      subtitle: 'Where archived tabs go hunting vs a sidebar that refuses to hide open state.',
      columns: [
        {
          header: 'Archive-first Sidebar',
          badge: 'Burial Risk',
          rows: [
            { k: 'Idle tabs', v: 'Moved to archive folders' },
            { k: 'Recall', v: 'Hunt through archive UI' },
            { k: 'Certainty', v: "Was that tab open? Unsure" }
          ]
        },
        {
          header: 'Maho Sidebar',
          badge: 'TabStripModel',
          highlight: true,
          rows: [
            { k: 'Idle tabs', v: 'Stay listed until closed' },
            { k: 'Recall', v: 'One glance, zero hunting' },
            { k: 'Certainty', v: 'UI matches runtime exactly' }
          ]
        },
        {
          header: 'Peek Overlay',
          badge: 'Commit Later',
          rows: [
            { k: 'Preview', v: 'Transient overlay first' },
            { k: 'Popups', v: 'Route to normal tabs' },
            { k: 'Focus', v: 'Never hijacked by auth windows' }
          ]
        }
      ]
    },
    body2: {
      tag: 'CONTEXT SWITCHING',
      title: 'Three rooms, one keystroke apart',
      subtitle: 'Spaces separate contexts cleanly; switching is instant and never relocates your tabs.',
      columns: [
        {
          header: 'Work Space',
          badge: 'Option+1',
          highlight: true,
          rows: [
            { k: 'Contents', v: 'Docs · dashboards · PRs' },
            { k: 'Session', v: 'Own tab set, own scroll' },
            { k: 'Return', v: 'Exactly as you left it' }
          ]
        },
        {
          header: 'Writing Space',
          badge: 'Option+2',
          rows: [
            { k: 'Contents', v: 'Research · drafts · references' },
            { k: 'Session', v: 'Isolated from Work' },
            { k: 'Noise', v: 'Zero cross-space leakage' }
          ]
        },
        {
          header: 'Personal Space',
          badge: 'Option+3',
          rows: [
            { k: 'Contents', v: 'Reading · media · shopping' },
            { k: 'Session', v: 'Separate history view' },
            { k: 'Boundary', v: 'Contexts never mix' }
          ]
        }
      ]
    }
  },
  {
    slug: 'maho-split-view-two-drags',
    hero: {
      tag: 'UX SERIES · SPLIT VIEW',
      protocol: 'Drag & Drop Gestures',
      title: 'Split view in two drags',
      subtitle: 'A gesture you learn once: drop a tab onto a tab for a clean split, or into the page grid for four quadrants with a live preview.',
      boxTitle: 'Gesture Map',
      boxTag: 'Muscle Memory',
      items: [
        { label: 'Tab → tab', value: 'Strict 2-way horizontal' },
        { label: 'Tab → viewport', value: '4-way grid + preview' },
        { label: 'Extra windows', value: 'Zero required' },
        { label: 'Drop feedback', value: 'Overlay before release' },
      ],
      bottomStatus: 'Deterministic Outcomes'
    },
    body1: {
      tag: 'TWO GESTURES',
      title: 'Two drags, two predictable outcomes',
      subtitle: 'The same motion, aimed at different targets, always produces the same layout.',
      columns: [
        {
          header: 'Drop onto a tab',
          badge: 'Sidebar Target',
          highlight: true,
          rows: [
            { k: 'Result', v: '50/50 horizontal split' },
            { k: 'Nesting', v: 'Impossible by design' },
            { k: 'Surprise', v: 'None — same every time' }
          ]
        },
        {
          header: 'Drop into page',
          badge: 'Viewport Target',
          rows: [
            { k: 'Result', v: 'Four-quadrant grid slot' },
            { k: 'Preview', v: 'Live drop-target overlay' },
            { k: 'Commit', v: 'Only on release' }
          ]
        },
        {
          header: 'Window clutter',
          badge: 'Eliminated',
          rows: [
            { k: 'New windows', v: 'Never spawned for splits' },
            { k: 'Mission control', v: 'One window stays one' },
            { k: 'Cleanup', v: 'Nothing to re-tile later' }
          ]
        }
      ]
    },
    body2: {
      tag: 'DIVIDER DISCIPLINE',
      title: 'Ratios adjust; focus never drifts',
      subtitle: 'Resize panes with the divider and keyboard flow, then keep both surfaces live side by side.',
      columns: [
        {
          header: 'Divider Drag',
          badge: '50/50 → 60/40',
          highlight: true,
          rows: [
            { k: 'Adjust', v: 'Smooth continuous ratio' },
            { k: 'Snap points', v: 'Predictable stops' },
            { k: 'Layout', v: 'Both panes stay live' }
          ]
        },
        {
          header: 'Keyboard Flow',
          badge: 'Palette First',
          rows: [
            { k: 'Navigation', v: 'Command palette targets pane' },
            { k: 'Hands', v: 'Stay on keys between panes' },
            { k: 'Mouse', v: 'Optional, not mandatory' }
          ]
        },
        {
          header: 'Real Workloads',
          badge: 'Docs × Dashboards',
          rows: [
            { k: 'Pairing', v: 'PR beside staging app' },
            { k: 'Reference', v: 'Notes beside video' },
            { k: 'Compare', v: 'Two docs, one glance' }
          ]
        }
      ]
    }
  },
  {
    slug: 'agentic-browsing-ordinary-hour',
    hero: {
      tag: 'UX SERIES · AGENTIC FLOW',
      protocol: 'Command Palette → Agent',
      title: 'An agent that asks permission',
      subtitle: 'An ordinary hour of browsing with AI that reads only with consent, explains its denials, and stops before it writes.',
      boxTitle: 'Consent Surface',
      boxTag: 'Always Shown',
      items: [
        { label: 'Read access', value: 'Opt-in, revocable' },
        { label: 'Writes', value: 'Typed approval prompt' },
        { label: 'Denials', value: 'Explain the reason code' },
        { label: 'Models', value: 'Local or BYOK keys' },
      ],
      bottomStatus: 'Houseguest That Knocks'
    },
    body1: {
      tag: 'READING WITH CONSENT',
      title: 'Agent reads only when you allow it',
      subtitle: 'Access starts off, stays scoped, and every denial tells you exactly why.',
      columns: [
        {
          header: 'Default State',
          badge: 'Reads Off',
          highlight: true,
          rows: [
            { k: 'Page reads', v: 'Require explicit opt-in' },
            { k: 'Mail broker', v: 'Three-gate consent chain' },
            { k: 'Revocation', v: 'One toggle, effective now' }
          ]
        },
        {
          header: 'Explained Denials',
          badge: 'Reason Codes',
          rows: [
            { k: 'Feature off', v: 'Says which toggle to flip' },
            { k: 'No consent', v: 'Names the missing consent' },
            { k: 'Silent failures', v: 'None — always a reason' }
          ]
        },
        {
          header: 'Scope',
          badge: 'Browser Only',
          rows: [
            { k: 'Surface', v: 'Tabs, pages, mail you choose' },
            { k: 'Desktop', v: 'No OS-level reach' },
            { k: 'Audit', v: 'Actions attributable' }
          ]
        }
      ]
    },
    body2: {
      tag: 'WRITES STOP THE SHOW',
      title: 'Typed approval before anything changes',
      subtitle: 'Mutations never inherit read permissions; rejecting a prompt is always safe.',
      columns: [
        {
          header: 'Mutation Attempt',
          badge: 'Approval Gate',
          highlight: true,
          rows: [
            { k: 'Trigger', v: 'Any write or account change' },
            { k: 'Prompt', v: 'Typed, specific request' },
            { k: 'On reject', v: 'Clean denial, no side effects' }
          ]
        },
        {
          header: 'Your Model, Your Keys',
          badge: 'Local / BYOK',
          rows: [
            { k: 'Models', v: 'Run local or bring API keys' },
            { k: 'Loop privacy', v: 'Task context stays yours' },
            { k: 'Telemetry', v: 'Zero by default' }
          ]
        },
        {
          header: 'After a Week',
          badge: 'Calmer Browsing',
          rows: [
            { k: 'Detours', v: 'Fewer copy-paste hops' },
            { k: 'Trust', v: 'Built by asking, not assuming' },
            { k: 'Flow', v: 'AI woven in, not bolted on' }
          ]
        }
      ]
    }
  }
];

for (const post of arcZenPosts) {
  await generatePng(
    renderHeroCard(post.hero),
    join(blogAssetsRoot, post.slug, 'hero.png')
  );
  await generatePng(
    renderDiagramCard(post.body1),
    join(blogAssetsRoot, post.slug, 'body-1.png')
  );
  await generatePng(
    renderDiagramCard(post.body2),
    join(blogAssetsRoot, post.slug, 'body-2.png')
  );
}

// 5. Maho vs Arc/Zen UX series 2 (boosts / vault / mobile / architecture)
const arcZenPosts2 = [
  {
    slug: 'maho-boosts-vs-arc-boosts',
    hero: {
      tag: 'UX SERIES · BOOSTS',
      protocol: 'Per-Origin Customization',
      title: 'Boosts without the middleman',
      subtitle: 'Per-site CSS, sandboxed JS, and settings knobs - applied by the browser you already trust.',
      boxTitle: 'Boost Anatomy',
      boxTag: 'Scoped Runtime',
      items: [
        { label: 'Host scope', value: 'One origin only' },
        { label: 'CSS', value: 'Standard stylesheet' },
        { label: 'JS', value: 'Sandboxed world' },
        { label: 'Settings', value: 'Panel knobs' },
      ],
      bottomStatus: 'Off Until You Enable'
    },
    body1: {
      tag: 'SCOPE DISCIPLINE',
      title: 'One origin, one grant',
      subtitle: 'A Boost runs only where you enable it - never across everything you visit.',
      columns: [
        { header: 'Enabled Here', badge: 'news.example', highlight: true, rows: [
          { k: 'CSS', v: 'Applies on load' },
          { k: 'JS', v: 'Sandboxed, post-DOM' },
          { k: 'Grant', v: 'This origin, this browser' }
        ]},
        { header: 'Everywhere Else', badge: 'Untouched', rows: [
          { k: 'Other sites', v: 'No effect, no reach' },
          { k: 'New tabs', v: 'Nothing injected' },
          { k: 'Cross-origin', v: 'Blocked by scope' }
        ]},
        { header: 'The Grant', badge: 'Per-Origin', rows: [
          { k: 'Default', v: 'Off until enabled' },
          { k: 'Revoke', v: 'One toggle, instant' },
          { k: 'Visibility', v: 'Scoped list, reviewable' }
        ]}
      ]
    },
    body2: {
      tag: 'SHARING',
      title: 'A Boost is a folder you own',
      subtitle: 'Local files with a manifest - copy it, back it up, hand it to a friend.',
      columns: [
        { header: 'Local First', badge: 'Your Disk', highlight: true, rows: [
          { k: 'Format', v: 'manifest + css + js' },
          { k: 'Backup', v: 'Any file sync you trust' },
          { k: 'Review', v: 'Read it before you run it' }
        ]},
        { header: 'No Update Channel', badge: 'No Surprise', rows: [
          { k: 'Changes', v: 'Only when you make them' },
          { k: 'Ownership', v: 'Never sold or swapped' },
          { k: 'Trust pivot', v: 'None required' }
        ]},
        { header: 'Handoff', badge: 'Copy & Go', rows: [
          { k: 'Share', v: 'A folder, not an account' },
          { k: 'Install', v: 'Drop in, enable per site' },
          { k: 'Audit', v: 'Plain files, plain view' }
        ]}
      ]
    }
  },
  {
    slug: 'maho-vault-password-flow',
    hero: {
      tag: 'UX SERIES · VAULT',
      protocol: 'Native Password Surface',
      title: 'Passwords where you browse',
      subtitle: 'A designed Vault in the browser itself - fill, update, and lock without leaving the page.',
      boxTitle: 'Daily Flow',
      boxTag: 'No Detours',
      items: [
        { label: 'Save', value: 'Inline, in flow' },
        { label: 'Update', value: 'One prompt' },
        { label: 'Autofill', value: 'No bubble detour' },
        { label: 'Agent access', value: 'Typed approval' },
      ],
      bottomStatus: 'Locked By Default'
    },
    body1: {
      tag: 'THE FILL FLOW',
      title: 'Save, update, fill - in place',
      subtitle: 'The credential moment happens where you already are.',
      columns: [
        { header: 'First Visit', badge: 'Save', highlight: true, rows: [
          { k: 'Prompt', v: 'Inline, post-signin' },
          { k: 'Stored to', v: 'Your Vault, one tap' },
          { k: 'Noise', v: 'No separate app launch' }
        ]},
        { header: 'Returning', badge: 'Fill', rows: [
          { k: 'Detection', v: 'Fields recognized in page' },
          { k: 'Fill', v: 'One gesture, no detour' },
          { k: 'Fallback', v: 'Vault surface one keystroke away' }
        ]},
        { header: 'Changed Password', badge: 'Update', rows: [
          { k: 'Detect', v: 'New credential on known origin' },
          { k: 'Resolve', v: 'Update or add, your call' },
          { k: 'History', v: 'Old value never lingers in UI' }
        ]}
      ]
    },
    body2: {
      tag: 'AGENT MEETS VAULT',
      title: 'Credentials never move without a prompt',
      subtitle: 'When the agent needs a secret, you see a typed request and decide.',
      columns: [
        { header: 'The Request', badge: 'Typed & Specific', highlight: true, rows: [
          { k: 'Shape', v: 'Named origin, named purpose' },
          { k: 'Scope', v: 'One credential, one task' },
          { k: 'Surprise', v: 'Never implicit' }
        ]},
        { header: 'The Decision', badge: 'You Hold It', rows: [
          { k: 'Approve', v: 'Explicit, per request' },
          { k: 'Reject', v: 'Always available' },
          { k: 'Default', v: 'No standing grants' }
        ]},
        { header: 'The Denial', badge: 'Explained', rows: [
          { k: 'On reject', v: 'Agent sees a reason code' },
          { k: 'On retry', v: 'New prompt, new decision' },
          { k: 'Silent failures', v: 'None' }
        ]}
      ]
    }
  },
  {
    slug: 'maho-mobile-experience',
    hero: {
      tag: 'UX SERIES · MOBILE',
      protocol: 'iOS + Android Beta',
      title: 'The browser you know, in your pocket',
      subtitle: 'Sync, reader mode, and a consent-first agent - the same Maho, pocket-sized.',
      boxTitle: 'Carried Over',
      boxTag: 'Desktop DNA',
      items: [
        { label: 'Sync', value: 'E2E encrypted' },
        { label: 'Reader', value: 'Font, size, sepia' },
        { label: 'Agent', value: 'Same consent gates' },
        { label: 'Spaces', value: 'Same philosophy' },
      ],
      bottomStatus: 'Beta, Iterating Fast'
    },
    body1: {
      tag: 'CONTINUITY',
      title: 'Leave the desk, keep the context',
      subtitle: 'A pairing handshake and encrypted relay move your tabs, not your privacy.',
      columns: [
        { header: 'Pair', badge: 'Once', highlight: true, rows: [
          { k: 'Handshake', v: 'Device to device' },
          { k: 'Keys', v: 'Stay on your hardware' },
          { k: 'Repeat', v: 'Never again' }
        ]},
        { header: 'Carry', badge: 'Encrypted', rows: [
          { k: 'Relay', v: 'Sees ciphertext only' },
          { k: 'Content', v: 'Tabs and context travel' },
          { k: 'Metadata', v: 'Minimal by design' }
        ]},
        { header: 'Arrive', badge: 'In Place', rows: [
          { k: 'On phone', v: 'Context waiting' },
          { k: 'On desk', v: 'Nothing lost' },
          { k: 'Both ways', v: 'Sync is bidirectional' }
        ]}
      ]
    },
    body2: {
      tag: 'READER MODE',
      title: 'Typography as a first-class mode',
      subtitle: 'Font, size, and theme controls built for reading, not squeezed into settings.',
      columns: [
        { header: 'Choose', badge: 'Typeface', highlight: true, rows: [
          { k: 'Fonts', v: 'Picker, not one default' },
          { k: 'Fit', v: 'Serif for prose, sans for scan' },
          { k: 'Respect', v: 'Page fallback preserved' }
        ]},
        { header: 'Adjust', badge: 'Comfort', rows: [
          { k: 'Size', v: 'Continuous slider' },
          { k: 'Theme', v: 'Dark, light, sepia' },
          { k: 'Remember', v: 'Per your preference' }
        ]},
        { header: 'Settle', badge: 'Read', rows: [
          { k: 'Chrome', v: 'Stripped to the article' },
          { k: 'Return', v: 'One tap back to the page' },
          { k: 'Focus', v: 'The point of mobile reading' }
        ]}
      ]
    }
  },
  {
    slug: 'maho-memory-cpu-architecture',
    hero: {
      tag: 'UX SERIES · ARCHITECTURE',
      protocol: 'Background Cost, Honestly',
      title: 'Where the memory goes',
      subtitle: 'Boosts instead of extensions, native UI, on-demand AI, zero telemetry - the choices that shape what runs when you are not looking.',
      boxTitle: 'What Runs Idle',
      boxTag: 'Design Choices',
      items: [
        { label: 'Boosts', value: 'No background pages' },
        { label: 'UI', value: 'Native Views' },
        { label: 'AI', value: 'On demand, local or BYOK' },
        { label: 'Telemetry', value: 'Zero by default' },
      ],
      bottomStatus: 'Method Over Scoreboard'
    },
    body1: {
      tag: 'EXTENSIONS VS BOOSTS',
      title: 'The always-on tax',
      subtitle: 'A background-page extension never sleeps. A Boost wakes with its origin and rests after.',
      columns: [
        { header: 'Extension Model', badge: 'Always Resident', highlight: true, rows: [
          { k: 'Process', v: 'May persist all session' },
          { k: 'Permissions', v: 'Broad, granted once' },
          { k: 'Cost', v: 'Paid whether used or not' }
        ]},
        { header: 'Boost Model', badge: 'Wake On Origin', rows: [
          { k: 'Process', v: 'Browser runtime, scoped' },
          { k: 'Permissions', v: 'One origin, per grant' },
          { k: 'Cost', v: 'Paid only while visiting' }
        ]},
        { header: 'What You Notice', badge: 'Over a Day', rows: [
          { k: 'Idle tabs', v: 'Fewer resident helpers' },
          { k: 'Startup', v: 'Fewer things to wake' },
          { k: 'Control', v: 'You see what is enabled' }
        ]}
      ]
    },
    body2: {
      tag: 'MEASURE IT YOURSELF',
      title: 'Your machine, your numbers',
      subtitle: 'Skip the scoreboard. Activity Monitor and per-site process views tell you what your setup really costs.',
      columns: [
        { header: 'Open', badge: 'Observe', highlight: true, rows: [
          { k: 'Tool', v: 'Activity Monitor / task manager' },
          { k: 'Sort', v: 'Memory, then CPU' },
          { k: 'Baseline', v: 'Note idle with one tab' }
        ]},
        { header: 'Compare', badge: 'Same Day', rows: [
          { k: 'Workload', v: 'Same sites, same tabs' },
          { k: 'Time', v: 'Same duration, same state' },
          { k: 'Extensions', v: 'Count them honestly' }
        ]},
        { header: 'Decide', badge: 'For Yourself', rows: [
          { k: 'Verdict', v: 'Yours, not ours' },
          { k: 'Tradeoffs', v: 'Features cost something' },
          { k: 'Recheck', v: 'After each change' }
        ]}
      ]
    }
  }
];

for (const post of arcZenPosts2) {
  await generatePng(
    renderHeroCard(post.hero),
    join(blogAssetsRoot, post.slug, 'hero.png')
  );
  await generatePng(
    renderDiagramCard(post.body1),
    join(blogAssetsRoot, post.slug, 'body-1.png')
  );
  await generatePng(
    renderDiagramCard(post.body2),
    join(blogAssetsRoot, post.slug, 'body-2.png')
  );
}

console.log('ALL BLOG IMAGES GENERATED SUCCESSFULLY!');
