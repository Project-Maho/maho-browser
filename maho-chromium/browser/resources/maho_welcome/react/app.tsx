import React, {useEffect, useRef, useState, useSyncExternalStore, useCallback} from 'react';
import {createRoot} from 'react-dom/client';

import {Button} from '@ui/button';
import {Input} from '@ui/input';
import {
  Select,
  SelectContent,
  SelectItem,
  SelectTrigger,
  SelectValue,
} from '@ui/select';
import {Tabs, TabsList, TabsTrigger} from '@ui/tabs';
import {cn} from '@lib/utils';

import {AppearanceContent, AppearanceSidebar} from './pages/appearance.js';
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
import type {AuthState, WelcomeState} from './types.js';

const WIZARD_STEPS = TOTAL_PAGES - 1;
const MAHO_LOGO_SRC = 'chrome://maho-welcome/icons/onboarding/maho-logo.png';

type AuthMode = AuthState['mode'];

const isStandalonePage = (page: WelcomePage) =>
  page === WelcomePage.Splash;

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
      isStandalonePage(renderedPage) && snapshot.currentPage !== renderedPage
      ? snapshot.currentPage
      : renderedPage;

  useEffect(() => {
    function handleKeyDown(event: KeyboardEvent) {
      if (event.defaultPrevented || event.repeat) return;
      const state = store.getSnapshot();
      if (state.currentPage === WelcomePage.Auth) {
        if (event.key === 'Enter') {
          if (event.target instanceof HTMLButtonElement) {
            return;
          }
          event.preventDefault();
          void submitAuth(store);
        }
        return;
      }
      if (event.key === 'Enter') {
        if (isStandalonePage(state.currentPage)) store.nextPage();
        else if (state.currentPage === WelcomePage.Completion) store.complete();
        else if (state.currentPage === WelcomePage.SourceSelect && state.importStage === 'A') store.openMigrationDialog();
        else store.nextPage();
      } else if (event.key === 'Escape') {
        if (state.currentPage !== WelcomePage.Completion &&
            !isStandalonePage(state.currentPage)) {
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
    if (isStandalonePage(snapshot.currentPage) ||
        isStandalonePage(renderedPage) ||
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
    if (isStandalonePage(pageForRender)) {
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
    return (
      <>
        {snapshot.isLocalDev && <DevSkipButton store={store} />}
        <WelcomeSplash onStart={() => store.nextPage()} />
      </>
    );
  }

  const isAuthPage = pageForRender === WelcomePage.Auth;
  const isCompletionPage = pageForRender === WelcomePage.Completion;
  const isSourceSelectPage = pageForRender === WelcomePage.SourceSelect;
  const canGoBack = !isAuthPage && !snapshot.auth.isReauthMode && (pageForRender > WelcomePage.Appearance ||
      (isSourceSelectPage && snapshot.importStage === 'B'));
  const currentStep = pageForRender - WelcomePage.Appearance;

  async function onPrimaryClick() {
    if (isAuthPage) {
      await submitAuth(store);
      return;
    }
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
      : isAuthPage
      ? getAuthSubmitLabel(snapshot.auth)
      : store.getString('IDS_MAHO_WELCOME_CTA_NEXT');

  const isDefaultBrowserStage =
      isSourceSelectPage && snapshot.importStage === 'B';
  const hideSidebarChrome = isAuthPage || snapshot.auth.isReauthMode;

  return (
    <main id="onboarding-pages" className="fixed inset-0 isolate flex min-h-screen w-full items-center justify-center overflow-hidden bg-background px-6 py-10 text-foreground">
      {snapshot.isLocalDev && <DevSkipButton store={store} />}
      <div aria-hidden="true" className="absolute inset-0 bg-gradient-to-br from-background via-background to-muted" />
      <div aria-hidden="true" className="absolute left-[18%] top-[12%] h-72 w-72 rounded-full bg-primary/10 blur-3xl" />
      <div aria-hidden="true" className="absolute bottom-0 right-0 h-96 w-96 translate-x-1/4 translate-y-1/4 rounded-full bg-muted/60 blur-3xl" />

      <div
        ref={shellRef}
        className="relative z-10 grid h-[min(720px,86vh)] w-[min(1040px,92vw)] overflow-hidden rounded-2xl border border-border bg-card shadow-2xl md:grid-cols-[0.42fr_0.58fr]"
      >
      <section className="flex min-h-0 flex-col border-b border-border bg-muted/30 md:border-b-0 md:border-r">
        <div
          ref={sidebarContentRef}
          className="flex min-h-0 flex-1 flex-col justify-center gap-4 p-8 lg:p-10 [&>h1]:m-0 [&>h1]:max-w-sm [&>h1]:text-3xl [&>h1]:font-semibold [&>h1]:leading-tight [&>h1]:tracking-tight [&>p]:m-0 [&>p]:max-w-sm [&>p]:text-sm [&>p]:leading-6 [&>p]:text-muted-foreground"
        >
          {renderSidebar(pageForRender, store, snapshot)}
        </div>
        <div
          ref={sidebarNavRef}
          className={cn(
              'flex shrink-0 flex-col items-start gap-2 border-t border-border/80 p-8 pt-6 transition-opacity lg:p-10 lg:pt-6',
              buttonsInteractive ? 'opacity-100' : 'opacity-70')}
          style={{pointerEvents: buttonsInteractive ? 'auto' : 'none'}}
        >
          {!hideSidebarChrome && (
            <StepIndicator current={currentStep} total={WIZARD_STEPS} />
          )}
          {!isCompletionPage && !hideSidebarChrome ? (
            <Button
              type="button"
              variant="ghost"
              size="sm"
              className="h-8 px-0 text-xs text-muted-foreground hover:bg-transparent hover:text-foreground"
              onClick={onSkipClick}
            >
              {store.getString('IDS_MAHO_WELCOME_CTA_SKIP')}
            </Button>
          ) : null}
          {canGoBack ? (
            <Button
              type="button"
              variant="outline"
              size="sm"
              className="h-8 rounded-lg border-border bg-background/70 text-xs"
              onClick={() => store.prevPage()}
            >
              {store.getString('IDS_MAHO_WELCOME_CTA_BACK')}
            </Button>
          ) : null}
        </div>
      </section>

      <section ref={contentRef} className="flex min-h-0 flex-col bg-card">
        <div className="flex min-h-0 flex-1 items-center justify-center overflow-hidden p-8 lg:p-10">
          <div
            ref={pageTransitionRef}
            key={pageForRender}
            className={cn(
                'flex h-full w-full items-center justify-center',
                snapshot.direction === 'forward' ? 'origin-right' : 'origin-left')}
          >
            {renderContent(pageForRender, snapshot, store)}
          </div>
        </div>
        <div
          ref={contentActionsRef}
          className={cn(
              'flex shrink-0 flex-wrap items-center justify-end gap-3 border-t border-border bg-background/30 px-8 py-5 transition-opacity lg:px-10',
              buttonsInteractive ? 'opacity-100' : 'opacity-70')}
          style={{pointerEvents: buttonsInteractive ? 'auto' : 'none'}}
        >
          {isSourceSelectPage && snapshot.importStage === 'A' && availableBrowsers.length > 0 && (
            <>
              <Select value={selectedBrowserName || undefined} onValueChange={handleBrowserChange}>
                <SelectTrigger className="h-10 w-44 rounded-lg border-border bg-background/70">
                  <SelectValue placeholder="Browser" />
                </SelectTrigger>
                <SelectContent>
                {uniqueBrowserNames.map(name => (
                  <SelectItem key={name} value={name}>
                    {name}
                  </SelectItem>
                ))}
                </SelectContent>
              </Select>
              {(() => {
                const profiles = parsedBrowsers.filter(b => b.browserName === selectedBrowserName);
                const shouldShowProfileSelect = profiles.length > 1 || (profiles.length === 1 && profiles[0].profileName !== '');
                if (!shouldShowProfileSelect) return null;
                return (
                  <Select
                    value={selectedBrowserIndex === null ? undefined : String(selectedBrowserIndex)}
                    onValueChange={value => setSelectedBrowserIndex(Number(value))}
                  >
                    <SelectTrigger className="h-10 w-40 rounded-lg border-border bg-background/70">
                      <SelectValue placeholder="Profile" />
                    </SelectTrigger>
                    <SelectContent>
                    {profiles.map(p => (
                      <SelectItem key={p.index} value={String(p.index)}>
                        {p.profileName}
                      </SelectItem>
                    ))}
                    </SelectContent>
                  </Select>
                );
              })()}
            </>
          )}
          {isDefaultBrowserStage && (
            <Button
              type="button"
              variant="ghost"
              className="h-10 rounded-lg text-muted-foreground hover:text-foreground"
              onClick={onMaybeLaterClick}
            >
              {store.getString('IDS_MAHO_WELCOME_DEFAULT_BROWSER_NO')}
            </Button>
          )}
          <Button
            type="button"
            className="h-10 min-w-36 rounded-lg font-semibold shadow-lg shadow-primary/10"
            aria-busy={isAuthPage && snapshot.auth.status === 'submitting'}
            disabled={isAuthPage && snapshot.auth.status === 'submitting'}
            onClick={() => { void onPrimaryClick(); }}
          >
            {primaryLabel}
          </Button>
        </div>
      </section>
      </div>
    </main>
  );
}

function renderSidebar(currentPage: WelcomePage, store: MahoWelcomeStore, snapshot: WelcomeState): React.ReactNode {
  switch (currentPage) {
    case WelcomePage.Auth:          return <AuthSidebar auth={snapshot.auth} />;
    case WelcomePage.Appearance:    return <AppearanceSidebar store={store} />;
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
    case WelcomePage.Auth:
      return <AuthContent auth={snapshot.auth} store={store} onSubmit={() => { void submitAuth(store); }} />;
    case WelcomePage.Appearance:
      return <AppearanceContent store={store} appearance={snapshot.appearance} />;
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
        <div className="flex h-full w-full flex-col items-center justify-center gap-4 rounded-xl border border-border bg-background/50 p-8 text-center">
          <div className="size-12 animate-spin rounded-full border-2 border-border border-t-primary" aria-hidden="true" />
          <p className="text-sm text-muted-foreground">Importing in progress...</p>
        </div>
      );
    case WelcomePage.SearchEngine:
      return (
        <SearchEngineContent
          engines={snapshot.searchEngines}
          selectedEngine={snapshot.selectedEngine}
          onSelect={keyword => store.setSearchEngine(keyword)}
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

function AuthSidebar({auth}: {auth: AuthState}) {
  const copy = getAuthSidebarCopy(auth);

  return (
    <>
      <img
        src={MAHO_LOGO_SRC}
        alt=""
        width={40}
        height={40}
        className="size-10 rounded-xl shadow-sm"
      />
      <h1>{copy.title}</h1>
      <p>{copy.body}</p>
    </>
  );
}

function AuthContent({auth, store, onSubmit}: {
  auth: AuthState;
  store: MahoWelcomeStore;
  onSubmit: () => void;
}) {
  const effectiveMode = getEffectiveAuthMode(auth);
  const isSignup = effectiveMode === 'signup';
  const isSubmitting = auth.status === 'submitting';

  return (
    <form
      className="mx-auto flex w-full max-w-sm flex-col gap-6"
      onSubmit={event => {
        event.preventDefault();
        onSubmit();
      }}
    >
      {!auth.hasStoredSession ? (
        <Tabs
          value={auth.mode}
          onValueChange={value => store.setAuthMode(value as AuthMode)}
        >
          <TabsList className="grid h-9 w-full grid-cols-2 bg-muted p-1">
            <TabsTrigger value="signin" className="h-7 text-xs">
              Sign in
            </TabsTrigger>
            <TabsTrigger value="signup" className="h-7 text-xs">
              Create account
            </TabsTrigger>
          </TabsList>
        </Tabs>
      ) : null}

      <div className="space-y-3">
        <Input
          aria-label="Email"
          autoComplete="email"
          className="h-11 bg-background text-foreground"
          disabled={isSubmitting}
          placeholder="Email"
          type="email"
          value={auth.email}
          onChange={event => store.setAuthEmail(event.target.value)}
        />

        <Input
          aria-label="Password"
          autoComplete={isSignup ? 'new-password' : 'current-password'}
          className="h-11 bg-background text-foreground"
          disabled={isSubmitting}
          placeholder="Password"
          type="password"
          value={auth.password}
          onChange={event => store.setAuthPassword(event.target.value)}
        />
      </div>

      {auth.status === 'error' ? (
        <p className="m-0 text-sm leading-5 text-destructive" role="alert">
          {auth.errorMessage}
        </p>
      ) : null}
    </form>
  );
}

async function submitAuth(store: MahoWelcomeStore): Promise<void> {
  const stateAuth = store.getSnapshot().auth;
  if (stateAuth.status === 'submitting') {
    return;
  }

  if (stateAuth.status === 'success') {
    finishAuthenticated(store, stateAuth.isReauthMode);
    return;
  }

  const mode = getEffectiveAuthMode(stateAuth);
  const didAuthenticate = mode === 'signin'
      ? await store.submitSignIn()
      : await store.submitSignUp();
  if (!didAuthenticate) {
    return;
  }

  finishAuthenticated(store, store.getSnapshot().auth.isReauthMode);
}

function finishAuthenticated(store: MahoWelcomeStore, isReauthMode: boolean) {
  if (isReauthMode) {
    store.complete();
    return;
  }

  store.nextPage();
}

function getEffectiveAuthMode(auth: AuthState): AuthMode {
  return auth.hasStoredSession ? 'signin' : auth.mode;
}

function getAuthSubmitLabel(auth: AuthState): string {
  return getEffectiveAuthMode(auth) === 'signup' ? 'Create account' : 'Sign in';
}

function getAuthSidebarCopy(auth: AuthState): {title: string; body: string} {
  if (auth.hasStoredSession) {
    return {
      title: 'Session expired',
      body: 'Sign in again to restore your Maho session.',
    };
  }

  if (auth.mode === 'signup') {
    return {
      title: 'Create your account',
      body: 'Set up a Maho account to continue.',
    };
  }

  return {
    title: 'Sign in to Maho',
    body: 'Sign in to continue setting up Maho.',
  };
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

function DevSkipButton({store}: {store: MahoWelcomeStore}) {
  return (
    <button
      type="button"
      onClick={() => store.devSkipOnboarding()}
      title="Local dev only — skip onboarding and open the browser"
      className="fixed bottom-3 left-3 z-50 rounded-md border border-dashed border-border/70 bg-background/80 px-2.5 py-1 text-[11px] font-medium text-muted-foreground shadow-sm backdrop-blur transition-colors hover:text-foreground"
    >
      Dev: Skip onboarding
    </button>
  );
}

function StepIndicator({current, total}: {current: number; total: number}) {
  return (
    <div className="mb-4 mt-1 flex gap-1.5">
      {Array.from({length: total}, (_, i) => (
        <div
          key={i}
          className={cn(
              'size-2 rounded-full bg-foreground transition-opacity',
              i === current ? 'opacity-80' : i < current ? 'opacity-40' : 'opacity-20')}
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
    </React.StrictMode>
);
