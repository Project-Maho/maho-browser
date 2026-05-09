import {
  AppState,
  getSessionAccessLabel,
  getRuntimeStateLabel,
  getSessionTitle,
  isSessionReadOnly,
} from '../types.js';
import {
  createEmptyPanel,
  createKeyValueRow,
  createPill,
  createSectionLabel,
  createSessionSummaryCard,
} from './cards.js';

export function renderInspector(state: AppState, handlers: {
  onRespondToApproval: (approvalId: string, approved: boolean) => void;
  onSelectInspector: (kind: AppState['selectedInspector']['kind'], id?: string) => void;
}): HTMLElement {
  const panel = document.createElement('aside');
  panel.className = 'console-inspector';

  const header = document.createElement('div');
  header.className = 'console-panel-header';
  const title = document.createElement('h2');
  title.textContent = 'Inspector';
  const subtitle = document.createElement('p');
  subtitle.textContent = 'Session, context, tool, and approval details.';
  header.append(title, subtitle);
  panel.appendChild(header);

  const tabs = document.createElement('div');
  tabs.className = 'inspector-tabs';
  const tabDefs: Array<AppState['selectedInspector']['kind']> = ['session', 'context', 'tool', 'approval'];
  for (const kind of tabDefs) {
    const button = document.createElement('button');
    button.className = 'inspector-tab';
    if (state.selectedInspector.kind === kind) {
      button.classList.add('is-active');
    }
    button.textContent = kind[0]!.toUpperCase() + kind.slice(1);
    button.addEventListener('click', () => handlers.onSelectInspector(kind, state.selectedInspector.id));
    tabs.appendChild(button);
  }
  panel.appendChild(tabs);

  const body = document.createElement('div');
  body.className = 'inspector-body';

  const currentSessionId = state.currentSessionId;
  const session = currentSessionId ? state.sessionsById[currentSessionId] : null;
  const readOnly = isSessionReadOnly(session);
  if (!session || !currentSessionId) {
    body.appendChild(createEmptyPanel('Nothing selected', 'Start a session to inspect runtime state.'));
    panel.appendChild(body);
    return panel;
  }

  if (state.selectedInspector.kind === 'session') {
    body.append(createSessionSummaryCard(session));
    const context = document.createElement('section');
    context.className = 'inspector-card';
    context.append(
        createSectionLabel('Context'),
        createKeyValueRow('Session', getSessionTitle(session)),
        createKeyValueRow('Access', getSessionAccessLabel(session)),
        createKeyValueRow('Runtime session', session.runtimeSessionId || 'Pending assignment'),
        createKeyValueRow('Last runtime state', getRuntimeStateLabel(session.lastRuntimeState)));
    body.appendChild(context);
  } else if (state.selectedInspector.kind === 'context') {
    const context = document.createElement('section');
    context.className = 'inspector-card';
    context.append(
        createSectionLabel('Attached context'),
        createKeyValueRow(
            'Browser context',
            readOnly ? 'Historical snapshot only' :
                       (state.composer.attachBrowserContext ? 'Attached on next prompt' : 'Detached')),
        createKeyValueRow('Composer mode', state.composer.modeLabel),
        createKeyValueRow('Pending turn', state.turnPendingBySessionId[currentSessionId] ? 'Yes' : 'No'));
    body.appendChild(context);
  } else if (state.selectedInspector.kind === 'tool') {
    const toolId = state.selectedInspector.id || Object.keys(state.toolCallsById).at(-1);
    const tool = toolId ? state.toolCallsById[toolId] : null;
    const result = toolId ? state.toolResultsByCallId[toolId] : null;
    if (!tool && !result) {
      body.appendChild(createEmptyPanel('No tool selected', 'Tool requests and results will appear here.'));
    } else {
      const card = document.createElement('section');
      card.className = 'inspector-card';
      card.append(createSectionLabel('Tool details'));
      if (tool) {
        card.append(
            createKeyValueRow('Tool', tool.toolName),
            createKeyValueRow('Call ID', tool.callId),
            createKeyValueRow('Arguments', tool.argumentsJson || '{}'));
      }
      if (result) {
        const resultBlock = document.createElement('pre');
        resultBlock.className = 'inspector-pre';
        resultBlock.textContent = result.errorMessage || result.output || 'No output';
        card.append(createPill(result.success ? 'Success' : 'Failure', result.success ? 'success' : 'danger'), resultBlock);
      }
      body.appendChild(card);
    }
  } else {
    const approvalId = state.selectedInspector.id || Object.keys(state.approvalsById).at(-1);
    const approval = approvalId ? state.approvalsById[approvalId] : null;
    const result = approvalId ? state.approvalResultsById[approvalId] : null;
    if (!approval) {
      body.appendChild(createEmptyPanel('No approval selected', 'Approval requests will appear here when the runtime pauses.'));
    } else {
      const card = document.createElement('section');
      card.className = 'inspector-card';
      card.append(
          createSectionLabel('Approval request'),
          createKeyValueRow('Approval ID', approval.approvalId),
          createKeyValueRow('Description', approval.description));

      if (approval.relatedToolCall) {
        card.append(createKeyValueRow('Related tool', approval.relatedToolCall.toolName));
      }

      if (result) {
        card.append(createPill(result.approved ? 'Approved' : 'Denied', result.approved ? 'success' : 'danger'));
        if (result.reason) {
          card.append(createKeyValueRow('Reason', result.reason));
        }
      } else {
        const actions = document.createElement('div');
        actions.className = 'inspector-actions';
        const approve = document.createElement('button');
        approve.className = 'console-button console-button--primary';
        approve.textContent = 'Approve';
        approve.disabled = readOnly;
        approve.addEventListener('click', () => handlers.onRespondToApproval(approval.approvalId, true));
        const deny = document.createElement('button');
        deny.className = 'console-button';
        deny.textContent = 'Deny';
        deny.disabled = readOnly;
        deny.addEventListener('click', () => handlers.onRespondToApproval(approval.approvalId, false));
        actions.append(approve, deny);
        card.appendChild(actions);
        if (readOnly) {
          card.appendChild(createEmptyPanel(
              'Historical approval',
              'This approval is shown from stored session history only and can no longer be answered from this view.'));
        }
      }

      body.appendChild(card);
    }
  }

  panel.appendChild(body);
  return panel;
}
