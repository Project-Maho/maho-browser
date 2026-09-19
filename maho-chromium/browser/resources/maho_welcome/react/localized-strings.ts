import {formatMailOnboardingString} from '../../maho_common/react/mail/mail_onboarding_api.js';
import type {MailOnboardingStringLookup} from '../../maho_common/react/mail/mail_onboarding_api.js';

export function createWelcomeMailStringLookup(
    strings: ReadonlyMap<string, string>): MailOnboardingStringLookup {
  return (key, fallback, values) => formatMailOnboardingString(
      strings.get(`IDS_MAHO_WELCOME_MAIL_${key.replaceAll('.', '_').toUpperCase()}`) ?? fallback,
      values);
}
