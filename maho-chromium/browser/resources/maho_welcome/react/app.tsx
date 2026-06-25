import React, {useEffect, useRef, useState, useSyncExternalStore, useCallback} from 'react';
import {createRoot} from 'react-dom/client';

import {CompletionContent, CompletionSidebar} from './pages/completion.js';
import {EssentialsContent, EssentialsSidebar} from './pages/essentials.js';
import {ImportDataContent, ImportDataSidebar} from './pages/import-data.js';
import {SearchEngineContent, SearchEngineSidebar} from './pages/search-engine.js';
import {WelcomeSplash} from './pages/welcome-splash.js';
import {
  PAGE_TRANSITION_ENTER,
  PAGE_TRANSITION_ENTER_BUTTON_START_DELAY,
  PAGE_TRANSITION_ENTER_STAGGER_BUTTONS,
  PAGE_TRANSITION_ENTER_STAGGER_SIDEBAR,
  PAGE_TRANSITION_EXIT,
  PAGE_TRANSITION_EXIT_DURATION,
  PAGE_TRANSITION_EXIT_START_DELAY,
} from './motion-constants.js';
import {MahoWelcomeStore} from './store.js';
import {TOTAL_PAGES, WelcomePage} from './types.js';
import type {WelcomeState} from './types.js';

const WIZARD_STEPS = TOTAL_PAGES - 1;

type AnimationPlaybackControls = {
  cancel: () => void;
  finished: Promise<void>;
};

function WelcomeApp({store}: {store: MahoWelcomeStore}) {
  const snapshot = useSyncExternalStore(
      listener => store.subscribe(listener), () => store.getSnapshot());
  const [renderedPage, setRenderedPage] = useState(snapshot.currentPage);
  const [buttonsInteractive, setButtonsInteractive] = useState(false);
  const [selectedBrowserIndex, setSelectedBrowserIndex] = useState<number | null>(null);
  const [selectedBrowserName, setSelectedBrowserName] = useState<string>('');

  const availableBrowsers = snapshot.availableBrowsers.length > 0
      ? snapshot.availableBrowsers
      : [
          {index: 0, name: 'Google Chrome', servicesSupported: 0},
          {index: 1, name: 'Safari', servicesSupported: 0},
          {index: 2, name: 'Mozilla Firefox', servicesSupported: 0},
        ];

  // Parse availableBrowsers into browserName and profileName
  const parsedBrowsers = availableBrowsers.map(b => {
    const parts = b.name.split(' - ');
    const browserName = parts[0];
    const profileName = parts.slice(1).join(' - ');
    return {
      ...b,
      browserName,
      profileName,
    };
  });

  const uniqueBrowserNames = Array.from(
      new Set(parsedBrowsers.map(b => b.browserName)));

  const handleBrowserChange = useCallback((browserName: string) => {
    setSelectedBrowserName(browserName);
    const matched = parsedBrowsers.find(b => b.browserName === browserName);
    if (matched) {
      setSelectedBrowserIndex(matched.index);
    }
  }, [parsedBrowsers]);

  useEffect(() => {
    if (availableBrowsers.length > 0) {
      const parsed = availableBrowsers.map(b => {
        const parts = b.name.split(' - ');
        return { index: b.index, browserName: parts[0] };
      });
      const uniqueNames = Array.from(new Set(parsed.map(b => b.browserName)));
      
      if (!selectedBrowserName || !uniqueNames.includes(selectedBrowserName)) {
        const firstBrowserName = uniqueNames[0];
        setSelectedBrowserName(firstBrowserName);
        const firstProfile = parsed.find(b => b.browserName === firstBrowserName);
        if (firstProfile) {
          setSelectedBrowserIndex(firstProfile.index);
        }
      } else if (selectedBrowserIndex === null) {
        const matchedProfile = parsed.find(b => b.browserName === selectedBrowserName);
        if (matchedProfile) {
          setSelectedBrowserIndex(matchedProfile.index);
        }
      }
    }
  }, [availableBrowsers, selectedBrowserName, selectedBrowserIndex]);
  const sidebarContentRef = useRef<HTMLDivElement>(null);
  const sidebarNavRef = useRef<HTMLDivElement>(null);
  const contentActionsRef = useRef<HTMLDivElement>(null);
  const contentRef = useRef<HTMLDivElement>(null);
  const shellRef = useRef<HTMLDivElement>(null);
  const pageTransitionRef = useRef<HTMLDivElement>(null);
  const activeAnimationsRef = useRef<AnimationPlaybackControls[]>([]);
  const transitionRunIdRef = useRef(0);
  const hasAnimatedShellRef = useRef(false);

  useEffect(() => { void store.bootstrap(); }, [store]);

  const pageForRender =
      renderedPage === WelcomePage.Splash &&
          snapshot.currentPage !== WelcomePage.Splash
      ? snapshot.currentPage
      : renderedPage;

  useEffect(() => {
    function handleKeyDown(event: KeyboardEvent) {
      if (event.defaultPrevented || event.repeat) return;
      if (event.key === 'Enter') {
        const state = store.getSnapshot();
        if (state.currentPage === WelcomePage.Splash) store.nextPage();
        else if (state.currentPage === WelcomePage.Completion) store.complete();
        else if (state.currentPage === WelcomePage.SourceSelect && state.importStage === 'A') store.openMigrationDialog();
        else store.nextPage();
      } else if (event.key === 'Escape') {
        const state = store.getSnapshot();
        if (state.currentPage !== WelcomePage.Completion &&
            state.currentPage !== WelcomePage.Splash) {
          if (state.currentPage === WelcomePage.SourceSelect) {
            store.skipImport();
          } else {
            store.nextPage();
          }
        }
      }
    }
    document.addEventListener('keydown', handleKeyDown);
    return () => document.removeEventListener('keydown', handleKeyDown);
  }, [store]);

  useEffect(() => {
    if (snapshot.currentPage === WelcomePage.Splash ||
        renderedPage === WelcomePage.Splash ||
        snapshot.currentPage === renderedPage) {
      return;
    }

    const runId = ++transitionRunIdRef.current;
    setButtonsInteractive(false);

    void runExitAnimation({
      sidebarContentRef,
      sidebarNavRef,
      contentActionsRef,
      contentRef,
      pageTransitionRef,
      activeAnimationsRef,
    }).then(() => {
      if (transitionRunIdRef.current !== runId) {
        return;
      }

      setRenderedPage(snapshot.currentPage);
    });
  }, [renderedPage, snapshot.currentPage]);

  useEffect(() => {
    if (pageForRender === WelcomePage.Splash) {
      return;
    }

    const runId = ++transitionRunIdRef.current;
    setButtonsInteractive(false);

    void runEntryAnimation({
      shellRef,
      sidebarContentRef,
      sidebarNavRef,
      contentActionsRef,
      contentRef,
      pageTransitionRef,
      activeAnimationsRef,
      animateShell: !hasAnimatedShellRef.current,
    }).then(() => {
      if (transitionRunIdRef.current !== runId) {
        return;
      }

      hasAnimatedShellRef.current = true;
      setButtonsInteractive(true);
    });
  }, [pageForRender]);

  const onSkipClick = useCallback(() => {
    if (snapshot.currentPage === WelcomePage.SourceSelect) {
      store.skipImport();
    } else {
      store.nextPage();
    }
  }, [snapshot.currentPage, store]);

  useEffect(() => () => stopAnimations(activeAnimationsRef.current), []);

  if (snapshot.currentPage === WelcomePage.Splash) {
    return <WelcomeSplash onStart={() => store.nextPage()} />;
  }

  const isCompletionPage = pageForRender === WelcomePage.Completion;
  const isSourceSelectPage = pageForRender === WelcomePage.SourceSelect;
  const canGoBack = pageForRender > WelcomePage.SourceSelect ||
      (isSourceSelectPage && snapshot.importStage === 'B');
  const currentStep = pageForRender - WelcomePage.SourceSelect;

  function onPrimaryClick() {
    if (isSourceSelectPage && snapshot.importStage === 'A') {
      if (snapshot.availableBrowsers.length > 0 && selectedBrowserIndex !== null) {
        store.startImport(selectedBrowserIndex);
      } else {
        void store.openMigrationDialog();
      }
      return;
    }
    if (isSourceSelectPage && snapshot.importStage === 'B') {
      store.setSetAsDefault(true);
      store.nextPage();
      return;
    }
    if (isCompletionPage) {
      store.complete();
      store.openMainBrowser();
      return;
    }
    store.nextPage();
  }

  function onMaybeLaterClick() {
    store.setSetAsDefault(false);
    store.nextPage();
  }

  const primaryLabel = isSourceSelectPage && snapshot.importStage === 'A'
      ? store.getString('IDS_MAHO_WELCOME_CTA_IMPORT')
      : isSourceSelectPage && snapshot.importStage === 'B'
      ? store.getString('IDS_MAHO_WELCOME_DEFAULT_BROWSER_YES')
      : isCompletionPage
      ? store.getString('IDS_MAHO_WELCOME_CTA_DONE')
      : store.getString('IDS_MAHO_WELCOME_CTA_NEXT');

  const isDefaultBrowserStage =
      isSourceSelectPage && snapshot.importStage === 'B';

  return (
    <div ref={shellRef} className="welcome-shell welcome-wizard">
      {/* SIDEBAR — left 40% */}
      <div className="welcome-shell__sidebar welcome-sidebar">
        {/* Top: title + body, grows */}
        <div
          ref={sidebarContentRef}
          className="welcome-shell__sidebar-content welcome-sidebar__content">
          {renderSidebar(pageForRender, store, snapshot)}
        </div>
        {/* Bottom: nav (dots + skip + back) */}
        <div
          ref={sidebarNavRef}
          className="welcome-shell__sidebar-nav welcome-sidebar__nav"
          style={{pointerEvents: buttonsInteractive ? 'auto' : 'none'}}>
          <StepIndicator current={currentStep} total={WIZARD_STEPS} />
          {!isCompletionPage ? (
            <button
              type="button"
              className="welcome-button welcome-button--skip"
              onClick={onSkipClick}>
              {store.getString('IDS_MAHO_WELCOME_CTA_SKIP')}
            </button>
          ) : null}
          {canGoBack ? (
            <button
              type="button"
              className="welcome-button welcome-button--back"
              onClick={() => store.prevPage()}>
              {store.getString('IDS_MAHO_WELCOME_CTA_BACK')}
            </button>
          ) : null}
        </div>
      </div>

      {/* CONTENT — right 60% */}
      <div ref={contentRef} className="welcome-shell__content welcome-content">
        {/* Top: page body, grows */}
        <div className="welcome-shell__content-body">
          <div
            ref={pageTransitionRef}
            key={pageForRender}
            className={`welcome-page-transition welcome-page-transition--${snapshot.direction}`}>
            {renderContent(pageForRender, snapshot, store)}
          </div>
        </div>
        {/* Bottom: actions row (primary right-aligned) */}
        <div
          ref={contentActionsRef}
          className="welcome-shell__content-actions welcome-content__actions"
          style={{pointerEvents: buttonsInteractive ? 'auto' : 'none'}}>
          {isSourceSelectPage && snapshot.importStage === 'A' && availableBrowsers.length > 0 && (
            <>
              <select
                className="welcome-select welcome-select--browser"
                value={selectedBrowserName}
                onChange={e => handleBrowserChange(e.target.value)}>
                {uniqueBrowserNames.map(name => (
                  <option key={name} value={name}>
                    {name}
                  </option>
                ))}
              </select>
              {(() => {
                const profiles = parsedBrowsers.filter(b => b.browserName === selectedBrowserName);
                const shouldShowProfileSelect = profiles.length > 1 || (profiles.length === 1 && profiles[0].profileName !== '');
                if (!shouldShowProfileSelect) return null;
                return (
                  <select
                    className="welcome-select welcome-select--profile"
                    value={selectedBrowserIndex ?? ''}
                    onChange={e => setSelectedBrowserIndex(Number(e.target.value))}>
                    {profiles.map(p => (
                      <option key={p.index} value={p.index}>
                        {p.profileName}
                      </option>
                    ))}
                  </select>
                );
              })()}
            </>
          )}
          {isDefaultBrowserStage && (
            <button
              type="button"
              className="welcome-button welcome-button--ghost"
              onClick={onMaybeLaterClick}>
              {store.getString('IDS_MAHO_WELCOME_DEFAULT_BROWSER_NO')}
            </button>
          )}
          <button
            type="button"
            className="welcome-button welcome-button--primary"
            onClick={onPrimaryClick}>
            {primaryLabel}
          </button>
        </div>
      </div>
    </div>
  );
}

function renderSidebar(currentPage: WelcomePage, store: MahoWelcomeStore, snapshot: WelcomeState): React.ReactNode {
  switch (currentPage) {
    case WelcomePage.SourceSelect:   return <ImportDataSidebar store={store} importStage={snapshot.importStage} />;
    case WelcomePage.ImportProgress:
      return (
        <>
          <h1>Importing your data...</h1>
          <p>Please wait while we bring over your bookmarks, history, and other data.</p>
        </>
      );
    case WelcomePage.SearchEngine:  return <SearchEngineSidebar store={store} />;
    case WelcomePage.Essentials:    return <EssentialsSidebar />;
    case WelcomePage.Completion:    return <CompletionSidebar />;
    case WelcomePage.Splash:        return null;
  }
}

function renderContent(
    currentPage: WelcomePage, snapshot: WelcomeState,
    store: MahoWelcomeStore): React.ReactNode {
  switch (currentPage) {
    case WelcomePage.SourceSelect:
      return (
        <ImportDataContent
          store={store}
          importStage={snapshot.importStage}
          setAsDefaultRequested={snapshot.setAsDefaultRequested}
          onSetAsDefaultChange={v => store.setSetAsDefault(v)}
        />
      );
    case WelcomePage.ImportProgress:
      return (
        <div className="welcome-import-progress">
          <div className="welcome-progress-spinner"></div>
          <p>Importing in progress...</p>
        </div>
      );
    case WelcomePage.SearchEngine:
      return (
        <SearchEngineContent
          engines={snapshot.searchEngines}
          selectedEngine={snapshot.selectedEngine}
          onSelect={keyword => store.setSearchEngine(keyword)}
          store={store}
        />
      );
    case WelcomePage.Essentials:
      return (
        <EssentialsContent
          sites={snapshot.essentialSites}
          selected={snapshot.selectedEssentials}
          onToggle={url => store.toggleEssential(url)}
        />
      );
    case WelcomePage.Completion:
      return <CompletionContent store={store} />;
    case WelcomePage.Splash:
      return null;
  }
}

function collectElements(collection: HTMLCollection | NodeListOf<Element>): Element[] {
  return Array.from(collection).filter((element): element is Element => !!element);
}

function getAnimationTiming(options: {
  bounce?: number;
  delay?: number;
  duration?: number;
}): KeyframeAnimationOptions {
  return {
    delay: (options.delay ?? 0) * 1000,
    duration: Math.max((options.duration ?? 0.45) * 1000, 1),
    easing: options.bounce && options.bounce > 0
        ? 'cubic-bezier(0.22, 1.18, 0.36, 1)'
        : 'cubic-bezier(0.32, 0.72, 0, 1)',
    fill: 'both',
  };
}

function animateElement(
    element: Element, keyframes: Keyframe[], options: {
      bounce?: number;
      delay?: number;
      duration?: number;
    }): AnimationPlaybackControls {
  const animation = element.animate(keyframes, getAnimationTiming(options));
  return {
    cancel: () => animation.cancel(),
    finished: animation.finished.then(() => undefined).catch(() => undefined),
  };
}

function createStaggerDelays(count: number, staggerDelay: number, startDelay = 0) {
  return Array.from({length: count}, (_, index) => startDelay + index * staggerDelay);
}

function stopAnimations(animations: AnimationPlaybackControls[]) {
  for (const animation of animations) {
    animation.cancel();
  }

  animations.length = 0;
}

function trackAnimation(
    animationsRef: React.MutableRefObject<AnimationPlaybackControls[]>,
    controls: AnimationPlaybackControls | null) {
  if (!controls) {
    return null;
  }

  animationsRef.current.push(controls);
  return controls;
}

async function awaitAnimations(animations: Array<AnimationPlaybackControls | null>) {
  await Promise.all(
      animations.filter((animation): animation is AnimationPlaybackControls => !!animation)
          .map(animation => animation.finished));
}

async function runExitAnimation({
  sidebarContentRef,
  sidebarNavRef,
  contentActionsRef,
  contentRef,
  pageTransitionRef,
  activeAnimationsRef,
}: {
  sidebarContentRef: React.RefObject<HTMLDivElement | null>;
  sidebarNavRef: React.RefObject<HTMLDivElement | null>;
  contentActionsRef: React.RefObject<HTMLDivElement | null>;
  contentRef: React.RefObject<HTMLDivElement | null>;
  pageTransitionRef: React.RefObject<HTMLDivElement | null>;
  activeAnimationsRef: React.MutableRefObject<AnimationPlaybackControls[]>;
}) {
  stopAnimations(activeAnimationsRef.current);

  const sidebarItems = sidebarContentRef.current ?
      collectElements(sidebarContentRef.current.children) : [];
  const contentItems = pageTransitionRef.current ? [pageTransitionRef.current] :
      (contentRef.current ? collectElements(contentRef.current.children) : []);
  const sidebarDelays = createStaggerDelays(
      sidebarItems.length, PAGE_TRANSITION_ENTER_STAGGER_SIDEBAR,
      PAGE_TRANSITION_EXIT_START_DELAY);
  const contentDelays = createStaggerDelays(
      contentItems.length, PAGE_TRANSITION_ENTER_STAGGER_SIDEBAR,
      PAGE_TRANSITION_EXIT_START_DELAY);

  await awaitAnimations([
    ...sidebarItems.map((item, index) =>
      trackAnimation(
          activeAnimationsRef,
          animateElement(item, [
            {opacity: 1},
            {opacity: 0},
          ], {
            ...PAGE_TRANSITION_EXIT,
            delay: sidebarDelays[index],
            duration: PAGE_TRANSITION_EXIT_DURATION,
          }))),
    ...contentItems.map((item, index) =>
      trackAnimation(
          activeAnimationsRef,
          animateElement(item, [
            {opacity: 1},
            {opacity: 0},
          ], {
            ...PAGE_TRANSITION_EXIT,
            delay: contentDelays[index],
            duration: PAGE_TRANSITION_EXIT_DURATION,
          }))),
  ]);

  activeAnimationsRef.current.length = 0;
}

async function runEntryAnimation({
  shellRef,
  sidebarContentRef,
  sidebarNavRef,
  contentActionsRef,
  contentRef,
  pageTransitionRef,
  activeAnimationsRef,
  animateShell,
}: {
  shellRef: React.RefObject<HTMLDivElement | null>;
  sidebarContentRef: React.RefObject<HTMLDivElement | null>;
  sidebarNavRef: React.RefObject<HTMLDivElement | null>;
  contentActionsRef: React.RefObject<HTMLDivElement | null>;
  contentRef: React.RefObject<HTMLDivElement | null>;
  pageTransitionRef: React.RefObject<HTMLDivElement | null>;
  activeAnimationsRef: React.MutableRefObject<AnimationPlaybackControls[]>;
  animateShell: boolean;
}) {
  stopAnimations(activeAnimationsRef.current);

  const sidebarItems = sidebarContentRef.current ?
      collectElements(sidebarContentRef.current.children) : [];
  const contentItems = pageTransitionRef.current ? [pageTransitionRef.current] :
      (contentRef.current ? collectElements(contentRef.current.children) : []);
  const sidebarDelays = createStaggerDelays(
      sidebarItems.length, PAGE_TRANSITION_ENTER_STAGGER_SIDEBAR);
  const contentDelays = createStaggerDelays(
      contentItems.length, PAGE_TRANSITION_ENTER_STAGGER_SIDEBAR);

  if (animateShell && shellRef.current) {
    const shellAnimation = trackAnimation(
        activeAnimationsRef,
        animateElement(shellRef.current, [{opacity: 0}, {opacity: 1}], {
          delay: 0.2,
          duration: 0.1,
        }));
    await awaitAnimations([shellAnimation]);
  }

  await awaitAnimations([
    ...sidebarItems.map((item, index) =>
      trackAnimation(
          activeAnimationsRef,
          animateElement(item, [
            {opacity: 0},
            {opacity: 1},
          ], {
            ...PAGE_TRANSITION_ENTER,
            delay: sidebarDelays[index],
          }))),
    ...contentItems.map((item, index) =>
      trackAnimation(
          activeAnimationsRef,
          animateElement(item, [
            {opacity: 0},
            {opacity: 1},
          ], {
            ...PAGE_TRANSITION_ENTER,
            delay: contentDelays[index],
          }))),
  ]);

  activeAnimationsRef.current.length = 0;
}

function StepIndicator({current, total}: {current: number; total: number}) {
  return (
    <div className="welcome-steps">
      {Array.from({length: total}, (_, i) => (
        <div
          key={i}
          className={`welcome-steps__dot${i === current ? ' is-active' : i < current ? ' is-completed' : ''}`}
        />
      ))}
    </div>
  );
}

const rootElement = document.getElementById('app');
if (!rootElement) throw new Error('Missing #app root for maho_welcome.');
const store = new MahoWelcomeStore();
// Expose for CDP-based visual regression testing.
// chrome:// pages are not web-accessible, so this is not a security surface.
(window as unknown as {__mahoWelcomeStore?: unknown}).__mahoWelcomeStore = store;
createRoot(rootElement).render(
    <React.StrictMode>
      <WelcomeApp store={store} />
    </React.StrictMode>);
