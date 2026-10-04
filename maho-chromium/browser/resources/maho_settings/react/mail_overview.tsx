// Copyright 2026 Maho Browser. All rights reserved.

// The top-level Mail pane. Mail used to live inside the Features pane, which
// buried the enable toggle two levels away from the sidebar. This pane is a
// first-class navigation entry that owns the Mail on/off switch, the AI read
// permission, notification and sync preferences, and the entry point into
// account management.
//
// Prefs are split across two backends by ownership:
//   * mail.enabled / ai.mail_read_allowed  -> SettingValue rows on the store.
//   * notifications / badge / sync interval -> mail helper behavior prefs.
// Behavior prefs are only reachable while Mail is enabled, so this pane renders
// them behind the same gate the sidebar uses.

import React, {useCallback, useEffect, useRef, useState} from 'react';

import {Button} from '@ui/button';
import {ArrowUpRight, Loader2} from '@icons/lucide';
import type {SettingValue} from '../mojo.js';
import {MailBehaviorUpdateStatus, MailNotificationPermission} from '../mojo.js';
import type {PaneDefinition} from '../models.js';
import {
  AiMailReadConsentSetting,
  ManagedSettingRow,
  PaneShell,
  SectionCard,
  SelectShell,
  Toggle,
} from './domain_panes.js';
import {
  DEFAULT_BEHAVIOR_PREFS,
  parseBehaviorSnapshot,
  SYNC_INTERVAL_OPTIONS,
  type BehaviorPrefs,
  type NotificationPreview,
} from './mail_behavior_prefs.js';
import {
  classifyMailSettingsFailure,
  type MailSettingsState,
} from './mail_settings_state.js';
import type {MahoSettingsStore} from './store.js';

export const MAIL_APP_URL = 'chrome://maho-mail';

const NOTIFICATION_PREVIEW_OPTIONS: Array<{
  value: NotificationPreview;
  label: string;
  description: string;
}> = [
  {
    value: 'sender_subject',
    label: 'Sender and subject',
    description: 'Show who sent the message and what it is about.',
  },
  {
    value: 'sender_only',
    label: 'Sender only',
    description: 'Show the sender and hide the subject line.',
  },
  {
    value: 'generic',
    label: 'Generic',
    description: 'Show only that new mail arrived.',
  },
];

function settingIsEnabled(settings: SettingValue[], key: string): boolean {
  return settings.some(setting => setting.key === key && setting.value === 'true');
}

function settingExists(settings: SettingValue[], key: string): boolean {
  return settings.some(setting => setting.key === key);
}

export function MailPane(
    {pane, settings, store}: {
      pane: PaneDefinition;
      settings: SettingValue[];
      store: MahoSettingsStore;
    }) {
  const mailEnabled = settingIsEnabled(settings, 'mail.enabled');
  const mailReadAllowed = settingIsEnabled(settings, 'ai.mail_read_allowed');
  const hasMailReadSetting = settingExists(settings, 'ai.mail_read_allowed');

  return (
    <PaneShell pane={pane}>
      <div className="space-y-6" data-mail-pane="overview">
        <SectionCard
          title="Maho Mail"
          description="Read and send mail from inside Maho. Turning Mail off hides the sidebar entry and stops background syncing.">
          <ManagedSettingRow
            title="Enable Maho Mail (Beta)"
            description="Turn on Maho Mail for this profile. You can turn it off at any time.">
            <Toggle
              enabled={mailEnabled}
              label="Enable Maho Mail (Beta)"
              onToggle={async next => {
                await store.commitSettingValue(
                    'mail.enabled', next ? 'true' : 'false');
              }}
            />
          </ManagedSettingRow>
          <ManagedSettingRow
            title="Mail accounts"
            description="Connect, reconnect, or remove mail accounts, and manage signatures, rules, calendars, and security keys.">
            <Button
              disabled={!mailEnabled}
              type="button"
              variant="outline"
              onClick={() => {
                window.open(MAIL_APP_URL, '_blank', 'noopener,noreferrer');
              }}>
              Open Maho Mail
              <ArrowUpRight aria-hidden="true" className="size-4 shrink-0" />
            </Button>
          </ManagedSettingRow>
        </SectionCard>

        <SectionCard
          title="Permissions"
          description="Control what Maho AI is allowed to do with your mail.">
          {hasMailReadSetting ? (
            <AiMailReadConsentSetting
              allowed={mailReadAllowed}
              onSave={async allowed => store.commitSettingValue(
                  'ai.mail_read_allowed', allowed ? 'true' : 'false')}
            />
          ) : (
            <ManagedSettingRow
              title="Allow AI to read Mail"
              description="The browser did not provide the ai.mail_read_allowed setting." />
          )}
        </SectionCard>

        {mailEnabled ? (
          <MailDeliverySection store={store} />
        ) : (
          <SectionCard
            title="Notifications & sync"
            description="Available once Maho Mail is enabled.">
            <ManagedSettingRow
              title="Notifications and sync are paused"
              description="Enable Maho Mail to choose notification previews, the unread badge, and how often mail syncs." />
          </SectionCard>
        )}
      </div>
    </PaneShell>
  );
}

// Notification, badge, and sync rows backed by the mail helper's behavior
// prefs. Mutations are optimistic with revision-checked reconciliation, matching
// MailBehaviorPane so the two surfaces cannot disagree.
function MailDeliverySection({store}: {store: MahoSettingsStore}) {
  const handler = store.getHandler();
  const router = store.getCallbackRouter();
  const mountedRef = useRef(true);
  const pendingKeysRef = useRef(new Set<keyof BehaviorPrefs>());
  const commitChainRef = useRef<Promise<void>>(Promise.resolve());
  const loadSequenceRef = useRef(0);
  const [prefs, setPrefs] = useState<BehaviorPrefs>(DEFAULT_BEHAVIOR_PREFS);
  const prefsRef = useRef(prefs);
  const [modelState, setModelState] =
      useState<MailSettingsState<BehaviorPrefs>>({status: 'loading'});
  const [error, setError] = useState('');
  const [notificationPermission, setNotificationPermission] =
      useState(MailNotificationPermission.kUnsupported);

  useEffect(() => {
    return () => {
      mountedRef.current = false;
    };
  }, []);

  const loadPrefs = useCallback(async () => {
    const sequence = ++loadSequenceRef.current;
    if (prefsRef.current.revision === 0) {
      setModelState({status: 'loading'});
    }
    try {
      const {ok, snapshot} = await handler.mailGetBehaviorPrefs();
      if (!mountedRef.current || sequence !== loadSequenceRef.current) return;
      if (!ok || !snapshot) {
        setModelState(classifyMailSettingsFailure(
            'Mail service unavailable', 'Failed to load mail delivery settings'));
        return;
      }
      const parsed =
          parseBehaviorSnapshot(snapshot.valueJson, Number(snapshot.revision));
      if (!parsed) {
        setModelState(
            {status: 'error', message: 'Failed to load mail delivery settings'});
        return;
      }
      if (pendingKeysRef.current.size > 0 ||
          parsed.revision < prefsRef.current.revision) {
        return;
      }
      prefsRef.current = parsed;
      setPrefs(parsed);
      setNotificationPermission(snapshot.notificationPermission);
      setModelState({status: 'ready', data: parsed});
    } catch {
      if (!mountedRef.current || sequence !== loadSequenceRef.current) return;
      setModelState(classifyMailSettingsFailure(
          '', 'Failed to load mail delivery settings'));
    }
  }, [handler]);

  useEffect(() => {
    void loadPrefs();
  }, [loadPrefs]);

  useEffect(() => {
    const listenerId =
        router.onMailBehaviorChanged.addListener(() => { void loadPrefs(); });
    return () => {
      router.removeListener(listenerId);
    };
  }, [router, loadPrefs]);

  const updatePref = useCallback(
      (key: keyof BehaviorPrefs, value: string|boolean|number) => {
        const previousValue = prefsRef.current[key];
        const optimistic = {...prefsRef.current, [key]: value} as BehaviorPrefs;
        prefsRef.current = optimistic;
        pendingKeysRef.current.add(key);
        setPrefs(optimistic);
        setModelState({status: 'ready', data: optimistic});
        setError('');

        const rollback = () => {
          const rolledBack = prefsRef.current[key] === value ?
              {...prefsRef.current, [key]: previousValue} as BehaviorPrefs :
              prefsRef.current;
          prefsRef.current = rolledBack;
          setPrefs(rolledBack);
          setError('Failed to update mail delivery preference');
        };

        commitChainRef.current = commitChainRef.current.then(async () => {
          try {
            const expectedRevision = prefsRef.current.revision;
            const {result} = await handler.mailSetBehaviorPref(
                BigInt(expectedRevision), key, JSON.stringify(value));
            const parsed = parseBehaviorSnapshot(
                result.snapshot.valueJson, Number(result.snapshot.revision));
            pendingKeysRef.current.delete(key);
            if (!mountedRef.current) return;
            setNotificationPermission(result.snapshot.notificationPermission);
            if (result.status === MailBehaviorUpdateStatus.kConflict) {
              const message =
                  'Mail settings changed in another tab. Review the latest values and try again.';
              setError(message);
              if (parsed) {
                prefsRef.current = parsed;
                setPrefs(parsed);
                setModelState({status: 'conflict', data: parsed, message});
              }
              return;
            }
            if (result.status !== MailBehaviorUpdateStatus.kApplied || !parsed) {
              rollback();
              return;
            }
            const reconciled = {...parsed};
            for (const pendingKey of pendingKeysRef.current) {
              Object.assign(
                  reconciled, {[pendingKey]: prefsRef.current[pendingKey]});
            }
            prefsRef.current = reconciled;
            setPrefs(reconciled);
            setModelState({status: 'ready', data: reconciled});
          } catch {
            pendingKeysRef.current.delete(key);
            if (mountedRef.current) {
              rollback();
            }
          }
        });
      },
      [handler]);

  if (modelState.status === 'loading') {
    return (
      <SectionCard title="Notifications & sync">
        <div data-mail-settings-state={modelState.status}>
          <ManagedSettingRow
            title="Loading mail delivery settings"
            description="Reading notification and sync preferences from the mail service.">
            <Loader2 aria-hidden="true" className="size-4 animate-spin text-muted-foreground" />
          </ManagedSettingRow>
        </div>
      </SectionCard>
    );
  }

  if (modelState.status === 'unavailable' || modelState.status === 'error') {
    return (
      <SectionCard title="Notifications & sync">
        <div data-mail-settings-state={modelState.status}>
          <ManagedSettingRow
            title="Mail delivery settings are unavailable"
            description={modelState.message}>
            <Button
              size="sm"
              type="button"
              variant="outline"
              onClick={() => { void loadPrefs(); }}>
              Retry
            </Button>
          </ManagedSettingRow>
        </div>
      </SectionCard>
    );
  }

  const permissionCopy =
      notificationPermission === MailNotificationPermission.kDenied ?
      'Denied by the operating system. Enable notifications in system settings.' :
      notificationPermission === MailNotificationPermission.kUnsupported ?
      'System notification permission is not supported on this platform.' :
      notificationPermission === MailNotificationPermission.kGranted ?
      'Allowed by the operating system.' :
      notificationPermission === MailNotificationPermission.kPromptPending ?
      'Waiting for the operating system permission prompt.' :
      'The operating system has not asked for notification permission yet.';

  return (
    <SectionCard
      title="Notifications & sync"
      description="Choose how Maho Mail interrupts you and how often it checks for new messages.">
      <div data-mail-settings-state={modelState.status}>
        {error ? (
          <p
            className="px-4 pt-4 text-xs leading-5 text-destructive sm:px-6"
            role={modelState.status === 'conflict' ? 'alert' : undefined}>
            {error}
          </p>
        ) : null}

        <ManagedSettingRow
          title="Desktop notifications"
          description="Receive system notifications for incoming mail.">
          <Toggle
            disabled={pendingKeysRef.current.has('desktop_notifications')}
            enabled={prefs.desktop_notifications}
            label="Desktop notifications"
            onToggle={next => updatePref('desktop_notifications', next)}
          />
        </ManagedSettingRow>

        <ManagedSettingRow
          title="Notification preview"
          description="Choose how much message information appears in system notifications.">
          <SelectShell<NotificationPreview>
            ariaLabel="Notification preview"
            options={NOTIFICATION_PREVIEW_OPTIONS}
            value={prefs.notification_preview}
            onChange={value => updatePref('notification_preview', value)}
          />
        </ManagedSettingRow>

        <ManagedSettingRow
          title="Unread badge"
          description="Show the unread count on the sidebar Mail icon and the app icon.">
          <Toggle
            disabled={pendingKeysRef.current.has('unread_badge_enabled')}
            enabled={prefs.unread_badge_enabled}
            label="Unread badge"
            onToggle={next => updatePref('unread_badge_enabled', next)}
          />
        </ManagedSettingRow>

        <ManagedSettingRow
          title="Sync interval"
          description="How often Maho Mail polls your accounts for new messages.">
          <SelectShell<string>
            ariaLabel="Sync interval"
            options={SYNC_INTERVAL_OPTIONS.map(
                option => ({label: option.label, value: String(option.value)}))}
            value={String(prefs.sync_interval)}
            onChange={value => updatePref('sync_interval', Number(value))}
          />
        </ManagedSettingRow>

        <ManagedSettingRow
          title="System permission"
          description={<span role="status">{permissionCopy}</span>}>
          {notificationPermission === MailNotificationPermission.kNotDetermined ? (
            <Button
              size="sm"
              type="button"
              variant="outline"
              onClick={async () => {
                const {permission} =
                    await handler.mailRequestNotificationPermission();
                if (mountedRef.current) setNotificationPermission(permission);
              }}>
              Allow notifications
            </Button>
          ) : null}
        </ManagedSettingRow>
      </div>
    </SectionCard>
  );
}
