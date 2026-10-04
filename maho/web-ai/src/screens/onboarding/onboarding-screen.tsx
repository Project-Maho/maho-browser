import { useState, useEffect, useRef } from 'preact/hooks';
import type { JSX } from 'preact';
import { useBridge } from '../../hooks/use-bridge';
import { useHashRoute } from '../../hooks/use-hash-route';
import { Icon } from '../../ui/icon';
import { MahoLogo } from '../../ui/maho-logo';
import './onboarding.css';

type OnboardingStep = 'splash' | 'auth' | 'theme' | 'default_browser' | 'completion';
type OnboardingTheme = 'system' | 'dark' | 'light';

function initialStep(step: string | undefined): OnboardingStep {
  return step === 'auth' ? 'auth' : 'splash';
}

function notifyNativeTheme(theme: OnboardingTheme) {
  const signal = document.createElement('iframe');
  signal.setAttribute('aria-hidden', 'true');
  signal.style.display = 'none';
  signal.src = `maho-theme://${theme}`;
  document.body.appendChild(signal);
  queueMicrotask(() => signal.remove());
}

export function OnboardingScreen() {
  const bridge = useBridge();
  const route = useHashRoute();
  const [step, setStep] = useState<OnboardingStep>(() => initialStep(route.params.step));

  const safeAreaTop = route.params.safeAreaTop ? parseFloat(route.params.safeAreaTop) : 0;
  const safeAreaBottom = route.params.safeAreaBottom ? parseFloat(route.params.safeAreaBottom) : 0;

  useEffect(() => {
    if (safeAreaTop > 0) {
      document.documentElement.style.setProperty('--safe-area-top', `${safeAreaTop}px`);
    }
    if (safeAreaBottom > 0) {
      document.documentElement.style.setProperty('--safe-area-bottom', `${safeAreaBottom}px`);
    }
  }, [safeAreaTop, safeAreaBottom]);

  useEffect(() => {
    const updateViewportHeight = () => {
      const height = window.visualViewport?.height ?? window.innerHeight;
      document.documentElement.style.setProperty('--onboarding-vh', `${height}px`);
    };

    updateViewportHeight();
    window.visualViewport?.addEventListener('resize', updateViewportHeight);
    window.visualViewport?.addEventListener('scroll', updateViewportHeight);
    window.addEventListener('resize', updateViewportHeight);

    return () => {
      window.visualViewport?.removeEventListener('resize', updateViewportHeight);
      window.visualViewport?.removeEventListener('scroll', updateViewportHeight);
      window.removeEventListener('resize', updateViewportHeight);
    };
  }, []);

  const [authMode, setAuthMode] = useState<'login' | 'signup'>('login');
  const [email, setEmail] = useState('');
  const [password, setPassword] = useState('');
  const [displayName, setDisplayName] = useState('');
  const [authLoading, setAuthLoading] = useState(false);
  const [authError, setAuthError] = useState<string | null>(null);
  const [selectedTheme, setSelectedTheme] = useState<OnboardingTheme>('system');

  const containerRef = useRef<HTMLDivElement>(null);
  useEffect(() => {
    if (containerRef.current && typeof containerRef.current.animate === 'function') {
      containerRef.current.animate(
        [
          { opacity: 0, transform: 'translateY(10px)' },
          { opacity: 1, transform: 'translateY(0)' },
        ],
        { duration: 400, easing: 'ease-out', fill: 'forwards' },
      );
    }

    if (step === 'splash' && containerRef.current) {
      const spans = containerRef.current.querySelectorAll('.animate-span');
      spans.forEach((span, i) => {
        const element = span as HTMLElement;
        if (typeof element.animate === 'function') {
          element.style.opacity = '0';
          element.animate(
            [
              { opacity: 0, transform: 'translateY(15px)' },
              { opacity: 1, transform: 'translateY(0)' },
            ],
            {
              duration: 800,
              delay: 200 + i * 600,
              easing: 'cubic-bezier(0.22, 1.18, 0.36, 1)',
              fill: 'forwards',
            },
          );
        }
      });
    }
  }, [step]);

  const handleAuthSubmit = async (e: JSX.TargetedEvent<HTMLFormElement, Event>) => {
    e.preventDefault();
    if (!email.trim() || !password.trim()) {
      setAuthError('Please fill in all fields.');
      return;
    }
    setAuthLoading(true);
    setAuthError(null);

    try {
      if (authMode === 'login') {
        const res = await bridge.relaySignIn(email, password);
        if (res.ok) {
          setStep('theme');
        } else {
          setAuthError(res.error || 'Failed to sign in.');
        }
      } else {
        const res = await bridge.relaySignUp(email, password, displayName);
        if (res.ok) {
          setStep('theme');
        } else {
          setAuthError(res.error || 'Failed to sign up.');
        }
      }
    } catch (err: any) {
      setAuthError(err.message || 'A network error occurred.');
    } finally {
      setAuthLoading(false);
    }
  };

  const handleGoogleSignIn = async () => {
    setAuthLoading(true);
    setAuthError(null);

    try {
      const res = await bridge.relaySignInWithGoogle();
      if (res.ok) {
        setStep('theme');
      } else if (!res.cancelled) {
        setAuthError(res.error || 'Failed to sign in with Google.');
      }
    } catch (err: any) {
      setAuthError(err.message || 'A network error occurred.');
    } finally {
      setAuthLoading(false);
    }
  };

  const handleThemeSelect = (theme: OnboardingTheme) => {
    setSelectedTheme(theme);
    notifyNativeTheme(theme);
  };

  const renderDots = () => {
    if (step === 'splash') return null;
    const steps: OnboardingStep[] = ['auth', 'theme', 'default_browser', 'completion'];
    const activeIndex = steps.indexOf(step);
    return (
      <div
        class="onboarding-dots"
        role="progressbar"
        aria-valuenow={activeIndex + 1}
        aria-valuemin={1}
        aria-valuemax={4}
        aria-label="Onboarding progress"
      >
        {steps.map((currentStep, idx) => (
          <span
            key={currentStep}
            class={`onboarding-dot ${idx === activeIndex ? 'active' : ''}`}
            aria-hidden="true"
          />
        ))}
      </div>
    );
  };

  return (
    <div
      ref={containerRef}
      class={`onboarding-container onboarding-step-${step}`}
      data-testid="onboarding-screen"
    >
      <main class="onboarding-main-content">
        {step === 'splash' && (
          <div class="onboarding-pane-splash">
            <div class="onboarding-logo-splash">
              <MahoLogo variant="star" size={160} class="onboarding-logo-star" />
            </div>
            <h1 class="onboarding-splash-title">
              <span class="animate-span block">Built for people who</span>
              <span class="animate-span block text-gradient">took browsers seriously.</span>
            </h1>
          </div>
        )}

        {step === 'auth' && (
          <div class="onboarding-pane-card">
            <div class="onboarding-pane-header">
              <h2>{authMode === 'login' ? 'Sign in to Maho' : 'Create your Maho AI account'}</h2>
              <p>
                {authMode === 'login'
                  ? 'Sign in to sync your browsing and use Maho AI. You can browse without an account.'
                  : 'Create a Maho AI account to sync your browsing. You can browse without an account.'}
              </p>
            </div>

            <div class="onboarding-tabs">
              <button
                type="button"
                class={`onboarding-tab ${authMode === 'login' ? 'active' : ''}`}
                onClick={() => {
                  setAuthMode('login');
                  setAuthError(null);
                }}
              >
                Sign In
              </button>
              <button
                type="button"
                class={`onboarding-tab ${authMode === 'signup' ? 'active' : ''}`}
                onClick={() => {
                  setAuthMode('signup');
                  setAuthError(null);
                }}
              >
                Create Account
              </button>
            </div>

            <form onSubmit={handleAuthSubmit} class="onboarding-form">
              {authError && (
                <div class="onboarding-error-banner" role="alert">
                  <Icon name="circle-alert" size={16} />
                  <span>{authError}</span>
                </div>
              )}

              {authMode === 'signup' && (
                <div class="onboarding-input-group">
                  <label for="display-name">Display Name</label>
                  <input
                    id="display-name"
                    type="text"
                    placeholder="Enter your name"
                    value={displayName}
                    disabled={authLoading}
                    onInput={(e) => setDisplayName(e.currentTarget.value)}
                  />
                </div>
              )}

              <div class="onboarding-input-group">
                <label for="auth-email">Email address</label>
                <input
                  id="auth-email"
                  type="email"
                  placeholder="name@example.com"
                  value={email}
                  disabled={authLoading}
                  required
                  onInput={(e) => setEmail(e.currentTarget.value)}
                />
              </div>

              <div class="onboarding-input-group">
                <label for="auth-password">Password</label>
                <input
                  id="auth-password"
                  type="password"
                  placeholder="Enter your password"
                  value={password}
                  disabled={authLoading}
                  required
                  onInput={(e) => setPassword(e.currentTarget.value)}
                />
              </div>

              <button type="submit" class="onboarding-btn-primary" disabled={authLoading}>
                {authLoading ? (
                  <Icon name="loader" class="animate-spin" size={18} />
                ) : authMode === 'login' ? (
                  'Sign in'
                ) : (
                  'Create account'
                )}
              </button>
            </form>

            <div class="onboarding-auth-divider" aria-hidden="true">
              <span />
              <span>or</span>
              <span />
            </div>

            <button
              type="button"
              class="onboarding-btn-google"
              disabled={authLoading}
              onClick={handleGoogleSignIn}
            >
              {authLoading ? <Icon name="loader" class="animate-spin" size={18} /> : <GoogleMark />}
              <span>Continue with Google</span>
            </button>

            <button
              type="button"
              class="onboarding-btn-text"
              disabled={authLoading}
              onClick={() => {
                setAuthError(null);
                setStep('theme');
              }}
            >
              Continue without an account
            </button>
          </div>
        )}

        {step === 'theme' && (
          <div class="onboarding-pane-card text-center flex-center">
            <div class="onboarding-icon-card">
              <Icon name="settings" size={44} class="onboarding-step-icon" aria-label="Theme" />
            </div>
            <div class="onboarding-pane-header">
              <h2>Choose your theme</h2>
              <p>Pick how Maho looks. You can change this later in Appearance settings.</p>
            </div>
            <div class="onboarding-tabs" aria-label="Theme selection">
              {(['system', 'dark', 'light'] as OnboardingTheme[]).map((theme) => (
                <button
                  key={theme}
                  type="button"
                  class={`onboarding-tab ${selectedTheme === theme ? 'active' : ''}`}
                  aria-pressed={selectedTheme === theme}
                  onClick={() => handleThemeSelect(theme)}
                >
                  {theme === 'system' ? 'System' : theme === 'dark' ? 'Dark' : 'Light'}
                </button>
              ))}
            </div>
          </div>
        )}

        {step === 'default_browser' && (
          <div class="onboarding-pane-card text-center flex-center">
            <div class="onboarding-icon-card">
              <Icon name="globe" size={44} class="onboarding-step-icon" aria-label="Globe" />
            </div>
            <div class="onboarding-pane-header">
              <h2>Set as default browser</h2>
              <p>Make Maho your go-to browser so every link opens right where you want it.</p>
            </div>
          </div>
        )}

        {step === 'completion' && (
          <div class="onboarding-pane-card text-center flex-center">
            <div class="onboarding-icon-card onboarding-check-pulse">
              <Icon
                name="circle-check"
                size={44}
                class="onboarding-step-icon onboarding-step-icon-success"
                aria-label="Circle check"
              />
            </div>
            <div class="onboarding-pane-header">
              <h2>You're all set</h2>
              <p>Maho AI, your theme, and browser setup are ready.</p>
            </div>
          </div>
        )}
      </main>

      <footer class="onboarding-footer">
        <div class="onboarding-footer-inner">
          {renderDots()}

          {step !== 'auth' && (
            <div class="onboarding-footer-actions">
              {['theme', 'default_browser'].includes(step) && (
                <button
                  type="button"
                  class="onboarding-btn-text"
                  onClick={() => {
                    if (step === 'theme') setStep('auth');
                    if (step === 'default_browser') setStep('theme');
                  }}
                >
                  <Icon name="chevron-left" size={16} />
                  <span>Back</span>
                </button>
              )}

              {step === 'splash' && (
                <button
                  type="button"
                  class="onboarding-btn-primary rounded-pill"
                  onClick={() => setStep('auth')}
                >
                  <span>Continue</span>
                  <Icon name="arrow-right" size={16} />
                </button>
              )}

              {step === 'theme' && (
                <button type="button" class="onboarding-btn-primary" onClick={() => setStep('default_browser')}>
                  Continue
                </button>
              )}

              {step === 'default_browser' && (
                <div class="onboarding-btn-group">
                  <button type="button" class="onboarding-btn-text" onClick={() => setStep('completion')}>
                    Maybe later
                  </button>
                  <button
                    type="button"
                    class="onboarding-btn-primary"
                    onClick={async () => {
                      await bridge.openDefaultBrowserSettings();
                      setStep('completion');
                    }}
                  >
                    Set as default
                  </button>
                </div>
              )}

              {step === 'completion' && (
                <button
                  type="button"
                  class="onboarding-btn-primary onboarding-btn-completion"
                  onClick={async () => {
                    await bridge.completeOnboarding();
                  }}
                >
                  <span>Dive in!</span>
                </button>
              )}
            </div>
          )}
        </div>
      </footer>
    </div>
  );
}

function GoogleMark() {
  return (
    <svg
      class="onboarding-google-mark"
      viewBox="0 0 18 18"
      aria-hidden="true"
      focusable="false"
    >
      <path
        fill="#EA4335"
        d="M17.64 9.205c0-.639-.057-1.253-.164-1.841H9v3.484h4.844a4.14 4.14 0 0 1-1.797 2.714v2.259h2.909c1.702-1.567 2.684-3.875 2.684-6.616Z"
      />
      <path
        fill="#4285F4"
        d="M9 18c2.43 0 4.467-.806 5.956-2.179l-2.909-2.259c-.806.54-1.837.858-3.047.858-2.344 0-4.328-1.584-5.037-3.71H.956v2.332A9 9 0 0 0 9 18Z"
      />
      <path
        fill="#FBBC05"
        d="M3.963 10.71A5.409 5.409 0 0 1 3.681 9c0-.594.102-1.171.282-1.71V4.959H.956A9 9 0 0 0 0 9c0 1.452.348 2.827.956 4.041l3.007-2.332Z"
      />
      <path
        fill="#34A853"
        d="M9 3.58c1.322 0 2.508.455 3.44 1.348l2.581-2.581C13.463.896 11.426 0 9 0A9 9 0 0 0 .956 4.959L3.963 7.29C4.672 5.164 6.656 3.58 9 3.58Z"
      />
    </svg>
  );
}
