import {MahoBoostStore} from '../store.js';
import {State} from '../types.js';
import {buildBoostCard} from './boost_card.js';
import {createButton, createElement, createMetaPill} from './elements.js';

export function buildBoostShell(
    state: State, store: MahoBoostStore): HTMLElement {
  const shell = createElement('div', 'boost-app-shell');
  shell.append(
      buildPageHeader(state),
      buildToolbar(state, store),
      buildStatus(state),
      buildBoostList(state, store));
  return shell;
}

function buildPageHeader(state: State): HTMLElement {
  const header = createElement('header', 'boost-page-header');
  const intro = document.createElement('div');
  const headerCopy = state.context ?
      `Manage boosts matching ${state.context.siteLabel}. Clear the page filter to return to all boosts.` :
      'Manage all boosts by URL pattern. Pick a boost to edit a single panel at a time in the compact shell.';
  intro.append(
      createElement('p', 'boost-page-eyebrow', 'Compact editor wave'),
      createElement('h1', 'boost-page-title', 'Boost manager'),
      createElement('p', 'boost-page-copy', headerCopy));

  const aside = createElement('div', 'boost-page-aside');
  const meta = createElement('div', 'boost-page-meta');
  meta.append(
      createMetaPill(`${state.boosts.length} boosts`),
      createMetaPill(`${state.templates.length} templates`));
  if (state.context) {
    meta.append(createMetaPill(`Current page · ${state.context.siteLabel}`));
  }
  aside.appendChild(meta);

  if (state.context) {
    aside.appendChild(createButton(
        'Show all boosts',
        'boost-button boost-button--secondary boost-button--small',
        false,
        clearCurrentPageContext));
  }

  header.append(intro, aside);
  return header;
}

function buildToolbar(state: State, store: MahoBoostStore): HTMLElement {
  const toolbar = createElement('section', 'boost-toolbar');
  toolbar.append(
      buildCreateBlankCard(state, store),
      buildCreateTemplateCard(state, store));
  return toolbar;
}

function buildCreateBlankCard(state: State, store: MahoBoostStore): HTMLElement {
  const card = createElement('div', 'boost-toolbar-card');
  const row = createElement('div', 'boost-inline-row');
  const input = document.createElement('input');
  input.id = 'create-pattern-input';
  input.className = 'boost-input';
  input.placeholder = '*://example.com/*';
  input.value = state.createPattern;
  input.disabled = state.busy;
  input.addEventListener('input', () => {
    store.setCreatePattern(input.value);
  });

  row.append(
      input,
      createButton(
          'Create',
          'boost-button boost-button--primary',
          state.busy,
          () => {
            void store.createBlankBoost();
          }));

  card.append(
      createElement(
          'h2',
          'boost-section-title',
          state.context ? 'Create boost for this site' : 'Create blank boost'),
      row);
  return card;
}

function buildCreateTemplateCard(state: State, store: MahoBoostStore): HTMLElement {
  const card = createElement('div', 'boost-toolbar-card');
  const grid = createElement('div', 'boost-toolbar-grid');

  const select = document.createElement('select');
  select.id = 'template-select';
  select.className = 'boost-input';
  select.disabled = state.busy;
  populateTemplateOptions(state, select);
  select.addEventListener('change', () => {
    store.setSelectedTemplateId(select.value);
  });

  const input = document.createElement('input');
  input.id = 'template-pattern-input';
  input.className = 'boost-input';
  input.placeholder = '*://docs.example.com/*';
  input.value = state.templatePattern;
  input.disabled = state.busy;
  input.addEventListener('input', () => {
    store.setTemplatePattern(input.value);
  });

  grid.append(
      select,
      input,
      createButton(
          'Use template',
          'boost-button boost-button--secondary',
          state.busy,
          () => {
            void store.createBoostFromTemplate();
          }));

  card.append(
      createElement(
          'h2',
          'boost-section-title',
          state.context ? 'Create template boost for this site' : 'Create from template'),
      grid);
  return card;
}

function buildStatus(state: State): HTMLElement {
  const status = createElement('div', 'boost-status');
  status.dataset['tone'] = state.statusTone;
  status.textContent = state.loading ? 'Loading…' : state.status;
  return status;
}

function buildBoostList(state: State, store: MahoBoostStore): HTMLElement {
  const list = createElement('main', 'boost-list-shell');
  if (state.boosts.length === 0) {
    list.appendChild(buildEmptyState(state));
    return list;
  }

  for (const boost of state.boosts) {
    list.appendChild(buildBoostCard(state, store, boost));
  }
  return list;
}

function populateTemplateOptions(state: State, select: HTMLSelectElement) {
  select.replaceChildren();
  if (state.templates.length === 0) {
    const option = document.createElement('option');
    option.value = '';
    option.textContent = 'No templates available';
    select.appendChild(option);
    return;
  }

  for (const template of state.templates) {
    const option = document.createElement('option');
    option.value = template.id;
    option.selected = template.id === state.selectedTemplateId;
    option.textContent = template.name;
    select.appendChild(option);
  }
}

function buildEmptyState(state: State): HTMLElement {
  const section = createElement('section', 'boost-empty-state');
  const emptyCopy = state.context ?
      'No boosts match this page yet. Use the suggested site pattern above to create one.' :
      'Create a blank boost for a pattern or pick a template to get started.';
  section.append(
      createElement('h2', 'boost-section-title', 'No boosts yet'),
      createElement('p', 'boost-empty-copy', emptyCopy));
  return section;
}

function clearCurrentPageContext() {
  const nextUrl = new URL(window.location.href);
  nextUrl.searchParams.delete('url');
  window.location.href = nextUrl.toString();
}
