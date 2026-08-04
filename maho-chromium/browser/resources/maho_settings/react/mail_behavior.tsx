// Copyright 2026 Maho Browser. All rights reserved.

import React, {useCallback, useEffect, useRef, useState} from 'react';
import {Loader2} from 'lucide-react';
import {Button} from '@ui/button';
import {Switch} from '@ui/switch';
import {Select, SelectContent, SelectItem, SelectTrigger, SelectValue} from '@ui/select';
import {PaneShell, SectionCard} from './domain_panes.js';
import {parseBehaviorPrefs, type BehaviorPrefs} from './mail_behavior_prefs.js';
import type {PaneDefinition} from '../models.js';
import type {MahoSettingsStore} from './store.js';

export function MailBehaviorPane({pane, store}: {pane: PaneDefinition; store: MahoSettingsStore}) {
  const handler = store.getHandler();
  const router = store.getCallbackRouter();
  const mountedRef = useRef(true);
  const retryCountRef = useRef(0);
  const retryTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const loadRef = useRef<() => void>(() => {});
  const [prefs, setPrefs] = useState<BehaviorPrefs>({
    sidebar_width: 250,
    email_list_width: 320,
    block_remote_images: true,
    block_trackers: true,
    desktop_notifications: true,
    sound_enabled: true,
    undo_send_delay: 5,
    auto_save_drafts: true,
    sync_interval: 15,
    email_body_font_family: 'system-ui',
    email_body_font_size: 14,
  });
  const [loading, setLoading] = useState(true);
  const [error, setError] = useState('');
  const [loadError, setLoadError] = useState('');

  useEffect(() => {
    return () => {
      mountedRef.current = false;
      if (retryTimerRef.current !== null) {
        clearTimeout(retryTimerRef.current);
        retryTimerRef.current = null;
      }
    };
  }, []);

  // Returns true while a bounded retry is pending (≤3), false once the cap is hit.
  const scheduleRetry = useCallback((): boolean => {
    if (retryCountRef.current >= 3) {
      return false;
    }
    retryCountRef.current += 1;
    if (retryTimerRef.current !== null) {
      clearTimeout(retryTimerRef.current);
    }
    retryTimerRef.current = setTimeout(() => {
      retryTimerRef.current = null;
      if (mountedRef.current) {
        loadRef.current();
      }
    }, 800);
    return true;
  }, []);

  const loadPrefs = useCallback(async () => {
    setLoading(true);
    try {
      const {ok, resultJson} = await handler.mailGetBehaviorPrefs();
      if (!mountedRef.current) return;
      if (ok && resultJson) {
        const parsed = parseBehaviorPrefs(resultJson);
        if (!parsed) {
          if (!scheduleRetry()) {
            setLoadError('Failed to load behavior settings');
            setLoading(false);
          }
          return;
        }
        setPrefs(parsed);
        setLoadError('');
        retryCountRef.current = 0;
        setLoading(false);
      } else if (!scheduleRetry()) {
        setLoadError('Failed to load behavior settings');
        setLoading(false);
      }
    } catch (e) {
      if (!mountedRef.current) return;
      if (!scheduleRetry()) {
        setLoadError('Failed to load behavior settings');
        setLoading(false);
      }
    }
  }, [handler, scheduleRetry]);

  useEffect(() => {
    loadRef.current = () => { void loadPrefs(); };
  }, [loadPrefs]);

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

  const updatePref = async (key: keyof BehaviorPrefs, value: string | boolean | number) => {
    const newPrefs = {...prefs, [key]: value};
    setPrefs(newPrefs);
    const {ok} = await handler.mailSetBehaviorPref(key, JSON.stringify(value));
    if (!ok) {
      setError('Failed to update behavior preference');
    }
  };

  return (
    <PaneShell pane={pane}>
      {loading ? (
        <p className="py-4 text-center text-sm text-muted-foreground"><Loader2 className="mr-2 inline size-4 animate-spin" />Loading behavior settings...</p>
      ) : (
        <div className="space-y-6">
          <SectionCard title="Mail Composition & Layout">
            {error && <p className="p-4 text-sm text-destructive">{error}</p>}
            {loadError && (
              <div className="flex flex-wrap items-center justify-between gap-3 p-4 text-sm text-muted-foreground">
                <span>{loadError}</span>
                <Button type="button" size="sm" variant="outline" onClick={() => { retryCountRef.current = 0; setLoadError(''); void loadPrefs(); }}>Retry</Button>
              </div>
            )}
            <div className="divide-y divide-border">
              <div className="flex items-center justify-between p-4">
                <div>
                  <h4 className="font-semibold text-sm">Undo Send Delay</h4>
                  <p className="text-xs text-muted-foreground">Delay sending messages to allow undo action.</p>
                </div>
                <Select value={String(prefs.undo_send_delay)} onValueChange={(val) => updatePref('undo_send_delay', Number(val))}>
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
                <Switch checked={prefs.auto_save_drafts} onCheckedChange={(checked) => updatePref('auto_save_drafts', checked)} />
              </div>

              <div className="flex items-center justify-between p-4">
                <div>
                  <h4 className="font-semibold text-sm">Email Body Font Family</h4>
                  <p className="text-xs text-muted-foreground">Choose standard font family for email message bodies.</p>
                </div>
                <Select value={prefs.email_body_font_family} onValueChange={(val) => updatePref('email_body_font_family', val)}>
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
                <Switch checked={prefs.block_remote_images} onCheckedChange={(checked) => updatePref('block_remote_images', checked)} />
              </div>

              <div className="flex items-center justify-between p-4">
                <div>
                  <h4 className="font-semibold text-sm">Block Email Trackers</h4>
                  <p className="text-xs text-muted-foreground">Scan and drop tracking pixels in incoming email body.</p>
                </div>
                <Switch checked={prefs.block_trackers} onCheckedChange={(checked) => updatePref('block_trackers', checked)} />
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
                <Select value={String(prefs.sync_interval)} onValueChange={(val) => updatePref('sync_interval', Number(val))}>
                  <SelectTrigger className="w-[180px]">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="5">Every 5 minutes</SelectItem>
                    <SelectItem value="15">Every 15 minutes</SelectItem>
                    <SelectItem value="30">Every 30 minutes</SelectItem>
                    <SelectItem value="60">Hourly</SelectItem>
                  </SelectContent>
                </Select>
              </div>

              <div className="flex items-center justify-between p-4">
                <div>
                  <h4 className="font-semibold text-sm">Desktop Notifications</h4>
                  <p className="text-xs text-muted-foreground">Receive system notification prompts for incoming emails.</p>
                </div>
                <Switch checked={prefs.desktop_notifications} onCheckedChange={(checked) => updatePref('desktop_notifications', checked)} />
              </div>
            </div>
          </SectionCard>
        </div>
      )}
    </PaneShell>
  );
}
