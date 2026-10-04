// Copyright 2026 Maho Browser. All rights reserved.

import React, {useCallback, useEffect, useRef, useState} from 'react';
import {Loader2} from 'lucide-react';
import {Button} from '@ui/button';
import {Switch} from '@ui/switch';
import {Select, SelectContent, SelectItem, SelectTrigger, SelectValue} from '@ui/select';
import {PaneShell, SectionCard} from './domain_panes.js';
import {DEFAULT_BEHAVIOR_PREFS, parseBehaviorSnapshot, SYNC_INTERVAL_OPTIONS, type BehaviorPrefs} from './mail_behavior_prefs.js';
import {classifyMailSettingsFailure, type MailSettingsState} from './mail_settings_state.js';
import {MailBehaviorUpdateStatus, MailNotificationPermission} from '../mojo.js';
import type {PaneDefinition} from '../models.js';
import type {MahoSettingsStore} from './store.js';

export function MailBehaviorPane({pane, store}: {pane: PaneDefinition; store: MahoSettingsStore}) {
  const handler = store.getHandler();
  const router = store.getCallbackRouter();
  const mountedRef = useRef(true);
  const pendingKeysRef = useRef(new Set<keyof BehaviorPrefs>());
  const commitChainRef = useRef<Promise<void>>(Promise.resolve());
  const loadSequenceRef = useRef(0);
  const [prefs, setPrefs] = useState<BehaviorPrefs>(DEFAULT_BEHAVIOR_PREFS);
  const prefsRef = useRef(prefs);
  const [modelState, setModelState] = useState<MailSettingsState<BehaviorPrefs>>(
      {status: 'loading'});
  const [error, setError] = useState('');

  const [notificationPermission, setNotificationPermission] = useState(
      MailNotificationPermission.kUnsupported);

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
      if (ok && snapshot) {
        const parsed = parseBehaviorSnapshot(
            snapshot.valueJson, Number(snapshot.revision));
        if (!parsed) {
          setModelState({status: 'error', message: 'Failed to load behavior settings'});
          return;
        }
        if (pendingKeysRef.current.size > 0 ||
            parsed.revision < prefsRef.current.revision) return;
        prefsRef.current = parsed;
        setPrefs(parsed);
        setNotificationPermission(snapshot.notificationPermission);
        setModelState({status: 'ready', data: parsed});
      } else {
        setModelState(classifyMailSettingsFailure(
            'Mail service unavailable', 'Failed to load behavior settings'));
      }
    } catch {
      if (!mountedRef.current || sequence !== loadSequenceRef.current) return;
      setModelState(classifyMailSettingsFailure(
          '', 'Failed to load behavior settings'));
    }
  }, [handler]);

  useEffect(() => {
    void loadPrefs();
  }, [loadPrefs]);

  useEffect(() => {
    const listenerId = router.onMailBehaviorChanged.addListener(() => {
      void loadPrefs();
    });
    return () => {
      router.removeListener(listenerId);
    };
  }, [router, loadPrefs]);

  const updatePref = (
      key: keyof BehaviorPrefs, value: string | boolean | number|string[]) => {
    const previousValue = prefsRef.current[key];
    const optimistic = {...prefsRef.current, [key]: value} as BehaviorPrefs;
    prefsRef.current = optimistic;
    pendingKeysRef.current.add(key);
    setPrefs(optimistic);
    setModelState({status: 'ready', data: optimistic});
    setError('');

    commitChainRef.current = commitChainRef.current.then(async () => {
      try {
        const expectedRevision = prefsRef.current.revision;
        const {result} = await handler.mailSetBehaviorPref(
            BigInt(expectedRevision), key, JSON.stringify(value));
        const parsed = parseBehaviorSnapshot(
            result.snapshot.valueJson, Number(result.snapshot.revision));
        pendingKeysRef.current.delete(key);
        setNotificationPermission(result.snapshot.notificationPermission);
        if (result.status === MailBehaviorUpdateStatus.kConflict) {
          const message = 'Behavior settings changed in another tab. Review the latest values and try again.';
          setError(message);
          if (parsed) {
            prefsRef.current = parsed;
            setPrefs(parsed);
            setModelState({status: 'conflict', data: parsed, message});
          }
        } else if (result.status !== MailBehaviorUpdateStatus.kApplied || !parsed) {
          const rolledBack = prefsRef.current[key] === value ?
              {...prefsRef.current, [key]: previousValue} as BehaviorPrefs :
              prefsRef.current;
          prefsRef.current = rolledBack;
          setPrefs(rolledBack);
          setError('Failed to update behavior preference');
        } else {
          const reconciled = {...parsed};
          for (const pendingKey of pendingKeysRef.current) {
            Object.assign(reconciled, {[pendingKey]: prefsRef.current[pendingKey]});
          }
          prefsRef.current = reconciled;
          setPrefs(reconciled);
          setModelState({status: 'ready', data: reconciled});
        }
      } catch {
        pendingKeysRef.current.delete(key);
        if (mountedRef.current) {
          const rolledBack = prefsRef.current[key] === value ?
              {...prefsRef.current, [key]: previousValue} as BehaviorPrefs :
              prefsRef.current;
          prefsRef.current = rolledBack;
          setPrefs(rolledBack);
          setError('Failed to update behavior preference');
        }
      }
    });
  };

  const permissionCopy = notificationPermission === MailNotificationPermission.kDenied ?
      'Denied by the operating system. Enable notifications in system settings.' :
      notificationPermission === MailNotificationPermission.kUnsupported ?
      'System notification permission is not supported on this platform.' :
      notificationPermission === MailNotificationPermission.kGranted ?
      'Allowed by the operating system.' :
      notificationPermission === MailNotificationPermission.kPromptPending ?
      'Waiting for the operating system permission prompt.' :
      'The operating system has not asked for notification permission yet.';

  return (
    <PaneShell pane={pane}>
      <div data-mail-settings-state={modelState.status}>
      {modelState.status === 'loading' ? (
        <p className="py-4 text-center text-sm text-muted-foreground"><Loader2 className="mr-2 inline size-4 animate-spin" />Loading behavior settings...</p>
      ) : modelState.status === 'unavailable' || modelState.status === 'error' ? (
        <div className="flex flex-wrap items-center justify-between gap-3 p-4 text-sm text-muted-foreground">
          <span>{modelState.message}</span>
          <Button type="button" size="sm" variant="outline" onClick={() => { void loadPrefs(); }}>Retry</Button>
        </div>
      ) : (
        <div className="space-y-6">
          <SectionCard title="Mail Composition & Layout">
            {error && <p className="p-4 text-sm text-destructive" role={modelState.status === 'conflict' ? 'alert' : undefined}>{error}</p>}

            <div className="divide-y divide-border">
              <div className="flex items-center justify-between p-4">
                <div>
                  <h4 className="font-semibold text-sm">Undo Send Delay</h4>
                  <p className="text-xs text-muted-foreground">Delay sending messages to allow undo action.</p>
                </div>
                <Select disabled={pendingKeysRef.current.has('undo_send_delay')} value={String(prefs.undo_send_delay)} onValueChange={(val) => updatePref('undo_send_delay', Number(val))}>
                  <SelectTrigger className="w-[180px]">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="0">Immediately</SelectItem>
                    <SelectItem value="5">5 seconds</SelectItem>
                    <SelectItem value="10">10 seconds</SelectItem>
                    <SelectItem value="30">30 seconds</SelectItem>
                  </SelectContent>
                </Select>
              </div>

              <div className="flex items-center justify-between p-4">
                <div>
                  <h4 className="font-semibold text-sm">Auto Save Drafts</h4>
                  <p className="text-xs text-muted-foreground">Automatically save draft email message while composing.</p>
                </div>
                <Switch disabled={pendingKeysRef.current.has('auto_save_drafts')} checked={prefs.auto_save_drafts} onCheckedChange={(checked) => updatePref('auto_save_drafts', checked)} />
              </div>

              <div className="flex items-center justify-between p-4">
                <div>
                  <h4 className="font-semibold text-sm">Email Body Font Family</h4>
                  <p className="text-xs text-muted-foreground">Choose standard font family for email message bodies.</p>
                </div>
                <Select disabled={pendingKeysRef.current.has('email_body_font_family')} value={prefs.email_body_font_family} onValueChange={(val) => updatePref('email_body_font_family', val)}>
                  <SelectTrigger className="w-[180px]">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="system-ui">System Default</SelectItem>
                    <SelectItem value="Georgia">Georgia (Serif)</SelectItem>
                    <SelectItem value="Courier New">Courier New (Mono)</SelectItem>
                  </SelectContent>
                </Select>
              </div>
            </div>
          </SectionCard>

          <SectionCard title="Privacy & Images">
            <div className="divide-y divide-border">
              <div className="flex items-center justify-between p-4">
                <div>
                  <h4 className="font-semibold text-sm">Block Remote Images</h4>
                  <p className="text-xs text-muted-foreground">Prevent loading external images inside emails dynamically.</p>
                </div>
                <Switch disabled={pendingKeysRef.current.has('block_remote_images')} checked={prefs.block_remote_images} onCheckedChange={(checked) => updatePref('block_remote_images', checked)} />
              </div>

              <div className="flex items-center justify-between p-4">
                <div>
                  <h4 className="font-semibold text-sm">Block Email Trackers</h4>
                  <p className="text-xs text-muted-foreground">Scan and drop tracking pixels in incoming email body.</p>
                </div>
                <Switch disabled={pendingKeysRef.current.has('block_trackers')} checked={prefs.block_trackers} onCheckedChange={(checked) => updatePref('block_trackers', checked)} />
              </div>
            </div>
          </SectionCard>

          <SectionCard title="Sync & Notifications">
            <div className="divide-y divide-border">
              <div className="flex items-center justify-between p-4">
                <div>
                  <h4 className="font-semibold text-sm">Sync Interval</h4>
                  <p className="text-xs text-muted-foreground">Interval for polling new emails.</p>
                </div>
                <Select disabled={pendingKeysRef.current.has('sync_interval')} value={String(prefs.sync_interval)} onValueChange={(val) => updatePref('sync_interval', Number(val))}>
                  <SelectTrigger className="w-[180px]">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    {SYNC_INTERVAL_OPTIONS.map(option => (
                      <SelectItem key={option.value} value={String(option.value)}>{option.label}</SelectItem>
                    ))}
                  </SelectContent>
                </Select>
              </div>

              <div className="flex items-center justify-between p-4">
                <div>
                  <h4 className="font-semibold text-sm">Desktop Notifications</h4>
                  <p className="text-xs text-muted-foreground">Receive system notification prompts for incoming emails.</p>
                </div>
                <Switch disabled={pendingKeysRef.current.has('desktop_notifications')} checked={prefs.desktop_notifications} onCheckedChange={(checked) => updatePref('desktop_notifications', checked)} />
              </div>

              <div className="flex items-center justify-between p-4">
                <div>
                  <h4 className="font-semibold text-sm">Notification Preview</h4>
                  <p className="text-xs text-muted-foreground">Choose how much message information appears in system notifications.</p>
                </div>
                <Select disabled={pendingKeysRef.current.has('notification_preview')} value={prefs.notification_preview} onValueChange={(value) => updatePref('notification_preview', value)}>
                  <SelectTrigger className="w-[180px]" aria-label="Notification preview">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="sender_subject">Sender and subject</SelectItem>
                    <SelectItem value="sender_only">Sender only</SelectItem>
                    <SelectItem value="generic">Generic</SelectItem>
                  </SelectContent>
                </Select>
              </div>

              <div className="flex items-center justify-between p-4">
                <div>
                  <h4 className="font-semibold text-sm">Unread Badge</h4>
                  <p className="text-xs text-muted-foreground">Show the unread count on the sidebar Mail icon and the app (Dock) icon.</p>
                </div>
                <Switch disabled={pendingKeysRef.current.has('unread_badge_enabled')} checked={prefs.unread_badge_enabled} onCheckedChange={(checked) => updatePref('unread_badge_enabled', checked)} />
              </div>

              <div className="flex items-center justify-between gap-4 p-4">
                <div>
                  <h4 className="font-semibold text-sm">System Permission</h4>
                  <p className="text-xs text-muted-foreground" role="status">{permissionCopy}</p>
                </div>
                {notificationPermission === MailNotificationPermission.kNotDetermined && (
                  <Button type="button" size="sm" variant="outline" onClick={async () => {
                    const {permission} = await handler.mailRequestNotificationPermission();
                    if (mountedRef.current) setNotificationPermission(permission);
                  }}>Allow notifications</Button>
                )}
              </div>
            </div>
          </SectionCard>
        </div>
      )}
      </div>
    </PaneShell>
  );
}
