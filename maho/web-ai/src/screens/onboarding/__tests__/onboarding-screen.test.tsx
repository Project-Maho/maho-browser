import { cleanup, fireEvent, render, screen, waitFor } from '@testing-library/preact';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';
import { OnboardingScreen } from '../onboarding-screen';

const mockBridge = {
  relaySignIn: vi.fn(),
  relaySignUp: vi.fn(),
  relayAccountStatus: vi.fn(),
  openDefaultBrowserSettings: vi.fn(),
  completeOnboarding: vi.fn(),
  byokGetKey: vi.fn(),
  byokSetKey: vi.fn(),
  byokDeleteKey: vi.fn(),
  byokValidateKey: vi.fn(),
};

const { mockRouteParams } = vi.hoisted(() => ({
  mockRouteParams: {
    safeAreaTop: '20',
    safeAreaBottom: '40',
  },
}));

vi.mock('../../../hooks/use-bridge', () => ({
  useBridge: () => mockBridge,
}));

// Mock useHashRoute to return mock route parameters
vi.mock('../../../hooks/use-hash-route', () => ({
  useHashRoute: () => ({
    name: 'onboarding',
    params: mockRouteParams,
  }),
}));

beforeEach(() => {
  mockRouteParams.safeAreaTop = '20';
  mockRouteParams.safeAreaBottom = '40';
  mockBridge.relaySignIn.mockResolvedValue({ ok: true });
  mockBridge.relaySignUp.mockResolvedValue({ ok: true });
  mockBridge.relayAccountStatus.mockResolvedValue({ hasValidSession: false, isReauth: false });
  mockBridge.openDefaultBrowserSettings.mockResolvedValue(undefined);
  mockBridge.completeOnboarding.mockResolvedValue(undefined);
  mockBridge.byokGetKey.mockResolvedValue(null);
  mockBridge.byokSetKey.mockResolvedValue(true);
  mockBridge.byokDeleteKey.mockResolvedValue(true);
  mockBridge.byokValidateKey.mockResolvedValue(true);
});

afterEach(() => {
  cleanup();
  vi.clearAllMocks();
  document.documentElement.style.removeProperty('--safe-area-top');
  document.documentElement.style.removeProperty('--safe-area-bottom');
});

describe('OnboardingScreen', () => {
  it('preserves native safe-area variables when route insets are zero', () => {
    // Given
    document.documentElement.style.setProperty('--safe-area-top', '62px');
    document.documentElement.style.setProperty('--safe-area-bottom', '34px');
    mockRouteParams.safeAreaTop = '0';
    mockRouteParams.safeAreaBottom = '0';

    // When
    render(<OnboardingScreen />);

    // Then
    expect(document.documentElement.style.getPropertyValue('--safe-area-top')).toBe('62px');
    expect(document.documentElement.style.getPropertyValue('--safe-area-bottom')).toBe('34px');
  });

  it('renders the canonical star-only Maho raster on the splash step', () => {
    // Given / When
    render(<OnboardingScreen />);

    // Then
    const logo = screen.getByTestId('maho-logo');
    expect(logo.tagName).toBe('IMG');
    expect(logo).toHaveAttribute('src', expect.stringMatching(/maho-star.*\.png/));
  });

  it('renders splash screen and advances to auth on continue click', async () => {
    render(<OnboardingScreen />);

    expect(screen.getByText('Built for people who')).toBeTruthy();
    expect(screen.getByText('took browsers seriously.')).toBeTruthy();

    const continueBtn = screen.getByRole('button', { name: /continue/i });
    fireEvent.click(continueBtn);

    expect(await screen.findByRole('heading', { name: /sign in to maho/i })).toBeTruthy();
    expect(screen.queryByTestId('maho-logo')).toBeNull();
  });

  it('displays validation error if email or password is empty in auth step', async () => {
    render(<OnboardingScreen />);
    fireEvent.click(screen.getByRole('button', { name: /continue/i }));

    const submitBtn = await screen.findByRole('button', { name: 'Sign in' });
    const form = submitBtn.closest('form');
    if (form) {
      fireEvent.submit(form);
    }

    expect(await screen.findByText('Please fill in all fields.')).toBeTruthy();
    expect(mockBridge.relaySignIn).not.toHaveBeenCalled();
  });

  it('handles successful login and transitions to default browser page', async () => {
    render(<OnboardingScreen />);
    fireEvent.click(screen.getByRole('button', { name: /continue/i }));

    const emailInput = await screen.findByLabelText(/email address/i);
    const passwordInput = screen.getByLabelText(/password/i);
    const submitBtn = screen.getByRole('button', { name: 'Sign in' });

    fireEvent.input(emailInput, { target: { value: 'user@test.com' } });
    fireEvent.input(passwordInput, { target: { value: 'password123' } });
    fireEvent.click(submitBtn);

    expect(await screen.findByRole('heading', { name: /set as default browser/i })).toBeTruthy();
    expect(mockBridge.relaySignIn).toHaveBeenCalledWith('user@test.com', 'password123');
    expect(screen.getByRole('img', { name: 'Globe' })).toBeTruthy();
    expect(screen.queryByTestId('maho-logo')).toBeNull();
  });

  it('displays API error banner if auth request fails', async () => {
    mockBridge.relaySignIn.mockResolvedValue({ ok: false, error: 'Invalid credentials' });
    render(<OnboardingScreen />);
    fireEvent.click(screen.getByRole('button', { name: /continue/i }));

    const emailInput = await screen.findByLabelText(/email address/i);
    const passwordInput = screen.getByLabelText(/password/i);
    const submitBtn = screen.getByRole('button', { name: 'Sign in' });

    fireEvent.input(emailInput, { target: { value: 'user@test.com' } });
    fireEvent.input(passwordInput, { target: { value: 'wrongpass' } });
    fireEvent.click(submitBtn);

    expect(await screen.findByText('Invalid credentials')).toBeTruthy();
  });

  it('navigates from default browser step to BYOK step', async () => {
    render(<OnboardingScreen />);
    fireEvent.click(screen.getByRole('button', { name: /continue/i }));

    const emailInput = await screen.findByLabelText(/email address/i);
    const passwordInput = screen.getByLabelText(/password/i);
    fireEvent.input(emailInput, { target: { value: 'user@test.com' } });
    fireEvent.input(passwordInput, { target: { value: 'pass123' } });
    fireEvent.click(screen.getByRole('button', { name: 'Sign in' }));

    // On default browser screen, click 'Maybe later'
    const maybeLaterBtn = await screen.findByRole('button', { name: /maybe later/i });
    fireEvent.click(maybeLaterBtn);

    expect(await screen.findByRole('heading', { name: /bring your own key/i })).toBeTruthy();
    expect(screen.queryByTestId('maho-logo')).toBeNull();
  });

  it('submits BYOK keys and finishes onboarding successfully', async () => {
    render(<OnboardingScreen />);
    // Navigate past splash, auth, and default browser
    fireEvent.click(screen.getByRole('button', { name: /continue/i }));

    const emailInput = await screen.findByLabelText(/email address/i);
    const passwordInput = screen.getByLabelText(/password/i);
    fireEvent.input(emailInput, { target: { value: 'user@test.com' } });
    fireEvent.input(passwordInput, { target: { value: 'pass123' } });
    fireEvent.click(screen.getByRole('button', { name: 'Sign in' }));

    fireEvent.click(await screen.findByRole('button', { name: /maybe later/i }));

    const openaiInput = await screen.findByLabelText(/openai api key/i);
    const anthropicInput = screen.getByLabelText(/anthropic api key/i);
    const saveBtn = screen.getByRole('button', { name: /save/i });

    fireEvent.input(openaiInput, { target: { value: 'sk-openai-key' } });
    fireEvent.input(anthropicInput, { target: { value: 'sk-anthropic-key' } });
    fireEvent.click(saveBtn);

    await waitFor(() => {
      expect(mockBridge.byokValidateKey).toHaveBeenCalledWith('openai', 'sk-openai-key');
      expect(mockBridge.byokValidateKey).toHaveBeenCalledWith('anthropic', 'sk-anthropic-key');
      expect(mockBridge.byokSetKey).toHaveBeenCalledWith('openai', 'sk-openai-key');
      expect(mockBridge.byokSetKey).toHaveBeenCalledWith('anthropic', 'sk-anthropic-key');
    });

    expect(await screen.findByRole('heading', { name: /you're all set/i })).toBeTruthy();
    expect(screen.getByRole('img', { name: 'Circle check' })).toBeTruthy();
    expect(screen.queryByTestId('maho-logo')).toBeNull();

    const diveInBtn = screen.getByRole('button', { name: /dive in!/i });
    fireEvent.click(diveInBtn);

    expect(mockBridge.completeOnboarding).toHaveBeenCalledOnce();
  });
});
