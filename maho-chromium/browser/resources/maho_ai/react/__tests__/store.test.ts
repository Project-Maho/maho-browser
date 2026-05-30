import {describe, it, expect, vi, beforeEach} from 'vitest';
import {MahoAiStore} from '../../store.js';
import {
  PageCallbackRouter,
  PageHandlerRemote,
  RuntimeConnectionState,
} from '../../maho_ai.mojom-webui.js';

describe('MahoAiStore — session resume and history fetch behavior', () => {
  let mockHandler: any;
  let mockRouter: any;
  let store: MahoAiStore;

  beforeEach(() => {
    // Standard mock setup
    mockHandler = new PageHandlerRemote();
    mockRouter = new PageCallbackRouter();

    // Mock connection state
    vi.spyOn(mockHandler, 'getConnectionState').mockResolvedValue({
      state: RuntimeConnectionState.kConnected,
      activeAdapterName: 'OpenCode',
    });

    // Mock session list returning a single session
    vi.spyOn(mockHandler, 'getSessionList').mockResolvedValue({
      sessions: [
        {
          sessionId: 'test-session-123',
          adapterName: 'OpenCode',
          isActive: true,
          isReadOnly: false,
          status: 1, // SessionStatus.kActive
          createdAt: Date.now() / 1000,
          updatedAt: Date.now() / 1000,
          title: 'Test Session',
          summary: '',
          eventCount: 0,
          toolCallCount: 0,
          lastRuntimeState: 'idle',
          runtimeSessionId: 'test-session-123',
        },
      ],
    });

    // Mock resume session return
    vi.spyOn(mockHandler, 'resumeSession').mockResolvedValue({
      session: {
        sessionId: 'test-session-123',
        adapterName: 'OpenCode',
        isActive: true,
        isReadOnly: false,
        status: 1,
        createdAt: Date.now() / 1000,
        updatedAt: Date.now() / 1000,
        title: 'Test Session',
        summary: '',
        eventCount: 0,
        toolCallCount: 0,
        lastRuntimeState: 'idle',
        runtimeSessionId: 'test-session-123',
      },
      replayEvents: [],
    });

    // Mock session history
    vi.spyOn(mockHandler, 'getSessionHistory').mockResolvedValue({
      events: [],
      totalCount: 0,
    });

    store = new MahoAiStore(mockHandler, mockRouter);
  });

  it('boots and automatically resumes session, loading its history', async () => {
    const resumeSessionSpy = vi.spyOn(store, 'resumeSession');
    const loadHistorySpy = vi.spyOn(store, 'loadHistory');

    await store.bootstrap();

    // Verify it automatically resumes the session
    expect(resumeSessionSpy).toHaveBeenCalledOnce();
    expect(resumeSessionSpy).toHaveBeenCalledWith('test-session-123');

    // Verify history was fetched because fetchHistory defaults to true
    expect(loadHistorySpy).toHaveBeenCalledOnce();
    expect(loadHistorySpy).toHaveBeenCalledWith('test-session-123');
    expect(mockHandler.getSessionHistory).toHaveBeenCalledWith('test-session-123', 0, 200);
  });

  it('loads history on explicit resumeSession call when fetchHistory defaults to true', async () => {
    const loadHistorySpy = vi.spyOn(store, 'loadHistory');

    // Bootstrap first (ignoring previous calls)
    await store.bootstrap();
    loadHistorySpy.mockClear();
    mockHandler.getSessionHistory.mockClear();

    // Call resumeSession directly without the second argument (fetchHistory should default to true)
    await store.resumeSession('test-session-123');

    expect(loadHistorySpy).toHaveBeenCalledOnce();
    expect(loadHistorySpy).toHaveBeenCalledWith('test-session-123');
    expect(mockHandler.getSessionHistory).toHaveBeenCalledWith('test-session-123', 0, 200);
  });

  it('does not load history if fetchHistory is explicitly set to false', async () => {
    const loadHistorySpy = vi.spyOn(store, 'loadHistory');

    await store.bootstrap();
    loadHistorySpy.mockClear();
    mockHandler.getSessionHistory.mockClear();

    // Call resumeSession with fetchHistory = false
    await store.resumeSession('test-session-123', false);

    expect(loadHistorySpy).not.toHaveBeenCalled();
    expect(mockHandler.getSessionHistory).not.toHaveBeenCalled();
  });

  it('searches bookmarks and handles result, and resets properly', async () => {
    vi.spyOn(mockHandler, 'searchBookmarks').mockResolvedValue({
      items: [
        {
          id: 'b1',
          url: 'https://google.com',
          title: 'Google',
        },
      ],
    });

    await store.bootstrap();

    // Initial state check
    expect(store.getSnapshot().bookmarksSearch.items).toEqual([]);

    // Trigger search
    const searchPromise = store.searchBookmarks('google');
    expect(store.getSnapshot().bookmarksSearch.loading).toBe(true);

    await searchPromise;

    expect(store.getSnapshot().bookmarksSearch.loading).toBe(false);
    expect(store.getSnapshot().bookmarksSearch.items).toEqual([
      {
        id: 'b1',
        url: 'https://google.com',
        title: 'Google',
      },
    ]);

    // Reset search
    store.resetBookmarksSearch();
    expect(store.getSnapshot().bookmarksSearch.items).toEqual([]);
    expect(store.getSnapshot().bookmarksSearch.query).toBe('');
  });
});
