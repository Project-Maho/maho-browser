import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/preact';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { OnboardingScreen } from '../onboarding-screen';

const mockBridge = {
  relaySignIn: vi.fn(),
  relaySignUp: vi.fn(),
  relaySignInWithGoogle: vi.fn(),
  relayAccountStatus: vi.fn(),
  openDefaultBrowserSettings: vi.fn(),
  completeOnboarding: vi.fn(),
};

const { mockRouteParams } = vi.hoisted(() => ({
  mockRouteParams: {
    safeAreaTop: '20',
    safeAreaBottom: '40',
  } as { safeAreaTop: string; safeAreaBottom: string; step?: string },
}));

vi.mock('../../../hooks/use-bridge', () => ({
  useBridge: () => mockBridge,
}));

vi.mock('../../../hooks/use-hash-route', () => ({
  useHashRoute: () => ({
    name: 'onboarding',
    params: mockRouteParams,
  }),
}));

beforeEach(() => {
  delete mockRouteParams.step;
  mockRouteParams.safeAreaTop = '20';
  mockRouteParams.safeAreaBottom = '40';
  mockBridge.relaySignIn.mockResolvedValue({ ok: true });
  mockBridge.relaySignUp.mockResolvedValue({ ok: true });
  mockBridge.relaySignInWithGoogle.mockResolvedValue({ ok: true });
  mockBridge.relayAccountStatus.mockResolvedValue({ hasValidSession: false, isReauth: false });
  mockBridge.openDefaultBrowserSettings.mockResolvedValue(undefined);
  mockBridge.completeOnboarding.mockResolvedValue(undefined);
});

afterEach(() => {
  cleanup();
  vi.clearAllMocks();
  document.documentElement.style.removeProperty('--safe-area-top');
  document.documentElement.style.removeProperty('--safe-area-bottom');
});

it('lets a user without an account continue past the auth step', async () => {
  render(<OnboardingScreen />);
  fireEvent.click(screen.getByRole('button', { name: /continue/i }));
  await screen.findByLabelText(/email address/i);

  fireEvent.click(screen.getByRole('button', { name: /continue without an account/i }));

  await screen.findByRole('heading', { name: /choose your theme/i });
  expect(mockBridge.relaySignIn).not.toHaveBeenCalled();
  expect(mockBridge.relaySignUp).not.toHaveBeenCalled();
  expect(mockBridge.relaySignInWithGoogle).not.toHaveBeenCalled();
});

async function advanceThroughLogin() {
  fireEvent.click(screen.getByRole('button', { name: /continue/i }));
  const emailInput = await screen.findByLabelText(/email address/i);
  const passwordInput = screen.getByLabelText(/password/i);
  fireEvent.input(emailInput, { target: { value: 'user@test.com' } });
  fireEvent.input(passwordInput, { target: { value: 'password123' } });
  fireEvent.click(screen.getByRole('button', { name: 'Sign in' }));
  await screen.findByRole('heading', { name: /choose your theme/i });
}

describe('OnboardingScreen', () => {
  it('opens directly at authentication for account recovery', () => {
    mockRouteParams.step = 'auth';

    render(<OnboardingScreen />);

    expect(screen.getByRole('heading', { name: /sign in to maho/i })).toBeTruthy();
    expect(screen.queryByText('Built for people who')).toBeNull();
  });

  it('preserves native safe-area variables when route insets are zero', () => {
    document.documentElement.style.setProperty('--safe-area-top', '62px');
    document.documentElement.style.setProperty('--safe-area-bottom', '34px');
    mockRouteParams.safeAreaTop = '0';
    mockRouteParams.safeAreaBottom = '0';

    render(<OnboardingScreen />);

    expect(document.documentElement.style.getPropertyValue('--safe-area-top')).toBe('62px');
    expect(document.documentElement.style.getPropertyValue('--safe-area-bottom')).toBe('34px');
  });

  it('renders the canonical star-only Maho raster on the splash step', () => {
    render(<OnboardingScreen />);

    const logo = screen.getByTestId('maho-logo');
    expect(logo.tagName).toBe('IMG');
    expect(logo).toHaveAttribute('src', expect.stringMatching(/maho-star.*\.png/));
  });

  it('renders splash screen and advances to auth on continue click', async () => {
    render(<OnboardingScreen />);

    expect(screen.getByText('Built for people who')).toBeTruthy();
    expect(screen.getByText('took browsers seriously.')).toBeTruthy();

    fireEvent.click(screen.getByRole('button', { name: /continue/i }));

    expect(await screen.findByRole('heading', { name: /sign in to maho/i })).toBeTruthy();
    expect(screen.queryByTestId('maho-logo')).toBeNull();
  });

  it('continues to theme selection after Google Maho AI sign-in', async () => {
    render(<OnboardingScreen />);

    fireEvent.click(screen.getByRole('button', { name: /continue/i }));
    fireEvent.click(await screen.findByRole('button', { name: /continue with google/i }));

    await waitFor(() => {
      expect(mockBridge.relaySignInWithGoogle).toHaveBeenCalledOnce();
    });
    expect(await screen.findByRole('heading', { name: /choose your theme/i })).toBeTruthy();
  });

  it('displays validation error if email or password is empty in auth step', async () => {
    render(<OnboardingScreen />);
    fireEvent.click(screen.getByRole('button', { name: /continue/i }));

    const submitBtn = await screen.findByRole('button', { name: 'Sign in' });
    fireEvent.submit(submitBtn.closest('form')!);

    expect(await screen.findByText('Please fill in all fields.')).toBeTruthy();
    expect(mockBridge.relaySignIn).not.toHaveBeenCalled();
  });

  it('handles successful login and transitions to theme selection', async () => {
    render(<OnboardingScreen />);
    await advanceThroughLogin();

    expect(mockBridge.relaySignIn).toHaveBeenCalledWith('user@test.com', 'password123');
    expect(screen.getByRole('img', { name: 'Theme' })).toBeTruthy();
    expect(screen.queryByTestId('maho-logo')).toBeNull();
  });

  it('displays API error banner if auth request fails', async () => {
    mockBridge.relaySignIn.mockResolvedValue({ ok: false, error: 'Invalid credentials' });
    render(<OnboardingScreen />);
    fireEvent.click(screen.getByRole('button', { name: /continue/i }));

    const emailInput = await screen.findByLabelText(/email address/i);
    const passwordInput = screen.getByLabelText(/password/i);
    fireEvent.input(emailInput, { target: { value: 'user@test.com' } });
    fireEvent.input(passwordInput, { target: { value: 'wrongpass' } });
    fireEvent.click(screen.getByRole('button', { name: 'Sign in' }));

    expect(await screen.findByText('Invalid credentials')).toBeTruthy();
  });

  it('selects a theme and advances to default-browser setup', async () => {
    render(<OnboardingScreen />);
    await advanceThroughLogin();

    fireEvent.click(screen.getByRole('button', { name: 'Dark' }));
    expect(screen.getByRole('button', { name: 'Dark' })).toHaveAttribute('aria-pressed', 'true');

    fireEvent.click(screen.getByRole('button', { name: 'Continue' }));
    expect(await screen.findByRole('heading', { name: /set as default browser/i })).toBeTruthy();
  });

  it('can skip default-browser setup and finish onboarding', async () => {
    render(<OnboardingScreen />);
    await advanceThroughLogin();

    fireEvent.click(screen.getByRole('button', { name: 'Continue' }));
    fireEvent.click(await screen.findByRole('button', { name: /maybe later/i }));

    expect(await screen.findByRole('heading', { name: /you're all set/i })).toBeTruthy();
    expect(screen.getByRole('img', { name: 'Circle check' })).toBeTruthy();

    fireEvent.click(screen.getByRole('button', { name: /dive in!/i }));
    expect(mockBridge.completeOnboarding).toHaveBeenCalledOnce();
  });

  it('opens native default-browser settings before finishing', async () => {
    render(<OnboardingScreen />);
    await advanceThroughLogin();

    fireEvent.click(screen.getByRole('button', { name: 'Continue' }));
    fireEvent.click(await screen.findByRole('button', { name: /set as default/i }));

    await waitFor(() => expect(mockBridge.openDefaultBrowserSettings).toHaveBeenCalledOnce());
    expect(await screen.findByRole('heading', { name: /you're all set/i })).toBeTruthy();
  });
});
