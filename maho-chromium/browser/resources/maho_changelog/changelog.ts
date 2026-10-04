// Copyright 2026 Maho Browser. All rights reserved.

export interface ChangelogSection {
  type: string;
  title: string;
  items: string[];
}

export interface ChangelogRelease {
  version: string;
  date: string;
  channel: string;
  highlights?: string[];
  sections?: ChangelogSection[];
  body?: string;
}

export interface ChangelogData {
  releases: ChangelogRelease[];
}

function escapeHtml(text: string): string {
  return text
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;')
    .replace(/'/g, '&#039;');
}

function renderInlineMarkdown(text: string): string {
  let out = escapeHtml(text);
  // Code `code`
  out = out.replace(/`([^`]+)`/g, '<code>$1</code>');
  // Bold **text** or __text__
  out = out.replace(/\*\*([^*]+)\*\*/g, '<strong>$1</strong>');
  out = out.replace(/__([^_]+)__/g, '<strong>$1</strong>');
  // Italic *text* or _text_
  out = out.replace(/(?<!\*)\*([^*]+)\*(?!\*)/g, '<em>$1</em>');
  out = out.replace(/(?<!_)_([^_]+)_(?!_)/g, '<em>$1</em>');
  // Links [label](url)
  out = out.replace(/\[([^\]]+)\]\(([^)]+)\)/g, (_match, label, rawUrl) => {
    const trimmed = rawUrl.trim();
    if (/^(https?:\/\/|mailto:|#|\/)/.test(trimmed)) {
      return `<a href="${trimmed}" target="_blank" rel="noopener noreferrer">${label}</a>`;
    }
    return `<a href="#">${label}</a>`;
  });
  return out;
}

function renderMarkdownBody(body: string): string {
  const lines = body.split(/\r?\n/);
  const htmlParts: string[] = [];
  let inCodeBlock = false;
  const codeLines: string[] = [];
  let inList = false;
  const listItems: string[] = [];
  let paragraphLines: string[] = [];

  const flushList = () => {
    if (inList && listItems.length > 0) {
      htmlParts.push(
        `<ul class="item-list">\n${listItems
          .map((it) => `  <li>${it}</li>\n`)
          .join('')}</ul>`
      );
      listItems.length = 0;
      inList = false;
    }
  };

  const flushParagraph = () => {
    if (paragraphLines.length > 0) {
      const combined = paragraphLines.join(' ').trim();
      if (combined) {
        htmlParts.push(`<p>${renderInlineMarkdown(combined)}</p>`);
      }
      paragraphLines = [];
    }
  };

  for (const line of lines) {
    const stripped = line.trim();

    if (stripped.startsWith('```')) {
      if (inCodeBlock) {
        flushParagraph();
        htmlParts.push(
          `<pre><code>${escapeHtml(codeLines.join('\n'))}</code></pre>`
        );
        codeLines.length = 0;
        inCodeBlock = false;
      } else {
        flushParagraph();
        flushList();
        inCodeBlock = true;
        codeLines.length = 0;
      }
      continue;
    }

    if (inCodeBlock) {
      codeLines.push(line);
      continue;
    }

    if (!stripped) {
      flushParagraph();
      flushList();
      continue;
    }

    const headerMatch = stripped.match(/^(#{1,6})\s+(.*)$/);
    if (headerMatch && headerMatch[1] && headerMatch[2]) {
      flushParagraph();
      flushList();
      const level = headerMatch[1].length;
      const text = renderInlineMarkdown(headerMatch[2].trim());
      htmlParts.push(`<h${level}>${text}</h${level}>`);
      continue;
    }

    const listMatch = stripped.match(/^[-*+]\s+(.*)$/);
    if (listMatch && listMatch[1]) {
      flushParagraph();
      inList = true;
      listItems.push(renderInlineMarkdown(listMatch[1].trim()));
      continue;
    }

    if (inList) {
      flushList();
    }
    paragraphLines.push(stripped);
  }

  if (inCodeBlock && codeLines.length > 0) {
    htmlParts.push(
      `<pre><code>${escapeHtml(codeLines.join('\n'))}</code></pre>`
    );
  }
  flushParagraph();
  flushList();

  return htmlParts.join('\n');
}

// Glyphs carry the change type so the surface keeps one neutral ink ramp
// plus the single violet accent, matching the standalone release-notes sheet.
const TYPE_GLYPHS: Record<string, string> = {
  added: '+',
  fixed: '\u2713',
  changed: '~',
  removed: '\u2212',
};

const TALLY_ORDER = ['added', 'fixed', 'changed', 'removed'] as const;

function getBadgeClass(type: string): string {
  switch (type.toLowerCase()) {
    case 'added':
      return 'badge-added';
    case 'fixed':
      return 'badge-fixed';
    case 'changed':
      return 'badge-changed';
    case 'removed':
      return 'badge-removed';
    default:
      return 'badge-general';
  }
}

export function renderReleaseCard(release: ChangelogRelease): HTMLElement {
  const card = document.createElement('article');
  card.className = 'release-card';

  // Header
  const header = document.createElement('header');
  header.className = 'release-header';

  const titleGroup = document.createElement('div');
  titleGroup.className = 'release-title-group';

  const versionEl = document.createElement('span');
  versionEl.className = 'release-version';
  versionEl.textContent = `Maho ${release.version || 'Unknown'}`;
  titleGroup.appendChild(versionEl);

  if (release.channel) {
    const channelBadge = document.createElement('span');
    channelBadge.className = 'badge badge-channel';
    channelBadge.textContent = release.channel;
    titleGroup.appendChild(channelBadge);
  }

  header.appendChild(titleGroup);

  if (release.date) {
    const dateEl = document.createElement('span');
    dateEl.className = 'release-date';
    dateEl.textContent = release.date;
    header.appendChild(dateEl);
  }

  card.appendChild(header);

  let hasContent = false;

  // Shape-of-release row: the size of a release reads before any prose does.
  const tallies = TALLY_ORDER.map(type => ({
    type,
    count: (release.sections || [])
        .filter(section => (section.type || '').toLowerCase() === type)
        .reduce((sum, section) => sum + (section.items || []).length, 0),
  })).filter(tally => tally.count > 0);

  if (tallies.length > 0) {
    hasContent = true;
    const glance = document.createElement('ul');
    glance.className = 'glance';
    const total = tallies.reduce((sum, tally) => sum + tally.count, 0);
    glance.setAttribute('aria-label', `${total} changes in this release`);
    for (const tally of tallies) {
      const cell = document.createElement('li');
      cell.className = 'tally';

      const glyph = document.createElement('span');
      glyph.className = 'tally-glyph';
      glyph.setAttribute('aria-hidden', 'true');
      glyph.textContent = TYPE_GLYPHS[tally.type] || '\u2022';
      cell.appendChild(glyph);

      const count = document.createElement('span');
      count.className = 'tally-count';
      count.textContent = String(tally.count);
      cell.appendChild(count);

      const label = document.createElement('span');
      label.className = 'tally-label';
      label.textContent = tally.type;
      cell.appendChild(label);

      glance.appendChild(cell);
    }
    card.appendChild(glance);
  }

  // Highlights
  const highlights = release.highlights || [];
  if (highlights.length > 0) {
    hasContent = true;
    const hlBox = document.createElement('div');
    hlBox.className = 'highlights-box';

    const hlTitle = document.createElement('div');
    hlTitle.className = 'highlights-title';
    hlTitle.textContent = 'In this release';
    hlBox.appendChild(hlTitle);

    const hlList = document.createElement('ol');
    hlList.className = 'item-list';
    for (const item of highlights) {
      const li = document.createElement('li');
      li.innerHTML = renderInlineMarkdown(item);
      hlList.appendChild(li);
    }
    hlBox.appendChild(hlList);
    card.appendChild(hlBox);
  }

  // Sections
  const sections = release.sections || [];
  const groups = document.createElement('div');
  groups.className = 'changes-grid';
  for (const section of sections) {
    const items = section.items || [];
    if (items.length === 0) {
      continue;
    }
    hasContent = true;

    const group = document.createElement('div');
    group.className = 'change-group';

    const changeHeader = document.createElement('div');
    changeHeader.className = 'change-header';

    const type = (section.type || 'general').toLowerCase();
    const badge = document.createElement('span');
    badge.className = `badge ${getBadgeClass(type)}`;

    const badgeGlyph = document.createElement('span');
    badgeGlyph.className = 'change-glyph';
    badgeGlyph.setAttribute('aria-hidden', 'true');
    badgeGlyph.textContent = TYPE_GLYPHS[type] || '\u2022';
    badge.appendChild(badgeGlyph);
    badge.appendChild(
        document.createTextNode((section.type || 'NOTE').toUpperCase()));
    changeHeader.appendChild(badge);

    if (section.title) {
      const title = document.createElement('span');
      title.className = 'change-title';
      title.textContent = section.title;
      changeHeader.appendChild(title);
    }

    const count = document.createElement('span');
    count.className = 'change-count';
    count.textContent = String(items.length);
    count.setAttribute('aria-label', `${items.length} changes`);
    changeHeader.appendChild(count);

    group.appendChild(changeHeader);

    const list = document.createElement('ul');
    list.className = 'item-list';
    for (const item of items) {
      const li = document.createElement('li');
      li.innerHTML = renderInlineMarkdown(item);
      list.appendChild(li);
    }
    group.appendChild(list);
    groups.appendChild(group);
  }
  if (groups.childElementCount > 0) {
    card.appendChild(groups);
  }

  // Body
  const bodyText = (release.body || '').trim();
  if (bodyText) {
    hasContent = true;
    const bodyEl = document.createElement('section');
    bodyEl.className = 'release-body';
    bodyEl.innerHTML = renderMarkdownBody(bodyText);
    card.appendChild(bodyEl);
  }

  // Graceful empty state for a release with no items
  if (!hasContent) {
    const emptyNotice = document.createElement('div');
    emptyNotice.className = 'release-empty-notice';
    emptyNotice.textContent = 'No detailed release notes provided for this version.';
    card.appendChild(emptyNotice);
  }

  return card;
}

export function renderEmptyState(message: string): HTMLElement {
  const empty = document.createElement('div');
  empty.className = 'empty-state';
  empty.textContent = message;
  return empty;
}

export async function initChangelog(containerId: string = 'app'): Promise<void> {
  const container = document.getElementById(containerId);
  if (!container) {
    return;
  }

  try {
    const res = await fetch('changelog.json');
    if (!res.ok) {
      throw new Error(`Failed to load changelog: HTTP ${res.status}`);
    }

    const data: ChangelogData = await res.json();
    const releases = data.releases || [];

    container.replaceChildren();

    if (releases.length === 0) {
      container.appendChild(
        renderEmptyState('No changelog entries found.')
      );
      return;
    }

    for (const release of releases) {
      container.appendChild(renderReleaseCard(release));
    }
  } catch (err) {
    container.replaceChildren();
    container.appendChild(
      renderEmptyState('Unable to load changelog at this time.')
    );
  }
}

if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', () => {
    initChangelog();
  });
} else {
  initChangelog();
}
