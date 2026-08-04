// Copyright 2026 Maho Browser. All rights reserved.

import React, {useCallback, useEffect, useRef, useState} from 'react';
import {Loader2, Check} from 'lucide-react';
import {Button} from '@ui/button';
import {Switch} from '@ui/switch';
import {Select, SelectContent, SelectItem, SelectTrigger, SelectValue} from '@ui/select';
import {PaneShell, SectionCard} from './domain_panes.js';
import type {PaneDefinition} from '../models.js';
import type {MahoSettingsStore} from './store.js';

interface CalendarPrefs {
  hide_weekends: boolean;
  week_start: number; // 0=Sunday, 1=Monday
  working_hours_start: string;
  working_hours_end: string;
  default_reminder_minutes: number;
}

interface CalendarCategory {
  id: string;
  name: string;
  color: string;
}

interface AccountCalendar {
  id: string;
  account_id: string;
  name: string;
  visible: boolean;
}

export function MailCalendarPane({pane, store}: {pane: PaneDefinition; store: MahoSettingsStore}) {
  const handler = store.getHandler();
  const router = store.getCallbackRouter();
  const mountedRef = useRef(true);
  const retryCountRef = useRef(0);
  const retryTimerRef = useRef<ReturnType<typeof setTimeout> | null>(null);
  const loadRef = useRef<() => void>(() => {});
  const [prefs, setPrefs] = useState<CalendarPrefs>({
    hide_weekends: false,
    week_start: 1,
    working_hours_start: '09:00',
    working_hours_end: '17:00',
    default_reminder_minutes: 15,
  });
  const [categories, setCategories] = useState<CalendarCategory[]>([]);
  const [calendars, setCalendars] = useState<AccountCalendar[]>([]);
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

  const loadCalendarData = useCallback(async () => {
    setLoading(true);
    try {
      const {ok: okPrefs, resultJson: prefsJson} = await handler.mailGetCalendarPrefs();
      if (!mountedRef.current) return;
      if (!okPrefs) {
        if (!scheduleRetry()) {
          setLoadError('Failed to load calendar settings');
          setLoading(false);
        }
        return;
      }
      if (prefsJson) {
        setPrefs(JSON.parse(prefsJson) as CalendarPrefs);
      }
      setLoadError('');
      retryCountRef.current = 0;

      const {ok: okCat, resultJson: catJson} = await handler.mailListCalendarCategories('');
      if (mountedRef.current) {
        if (okCat && catJson) {
          setCategories(JSON.parse(catJson) as CalendarCategory[]);
        } else {
          setError('Failed to load calendar categories');
        }
      }

      const {ok: okCal, resultJson: calJson} = await handler.mailListAccountCalendars('');
      if (mountedRef.current) {
        if (okCal && calJson) {
          setCalendars(JSON.parse(calJson) as AccountCalendar[]);
        } else {
          setError('Failed to load account calendars');
        }
        setLoading(false);
      }
    } catch (e) {
      if (!mountedRef.current) return;
      if (!scheduleRetry()) {
        setLoadError('Failed to load calendar settings');
        setLoading(false);
      }
    }
  }, [handler, scheduleRetry]);

  useEffect(() => {
    loadRef.current = () => { void loadCalendarData(); };
  }, [loadCalendarData]);

  useEffect(() => {
    void loadCalendarData();
  }, [loadCalendarData]);

  useEffect(() => {
    const listenerId = router.onMailCalendarChanged.addListener(() => {
      void loadCalendarData();
    });
    return () => {
      router.removeListener(listenerId);
    };
  }, [router, loadCalendarData]);

  const updatePrefs = async (newPrefs: CalendarPrefs) => {
    setPrefs(newPrefs);
    const {ok} = await handler.mailSetCalendarPrefs(JSON.stringify(newPrefs));
    if (!ok) {
      setError('Failed to update calendar preferences');
    }
  };

  const toggleCalendarVisibility = async (id: string, currentVisible: boolean) => {
    const {ok} = await handler.mailSetCalendarVisibility(id, !currentVisible);
    if (ok) {
      void loadCalendarData();
    } else {
      setError('Failed to toggle calendar visibility');
    }
  };

  return (
    <PaneShell pane={pane}>
      {loading ? (
        <p className="py-4 text-center text-sm text-muted-foreground"><Loader2 className="mr-2 inline size-4 animate-spin" />Loading calendar settings...</p>
      ) : (
        <div className="space-y-6">
          <SectionCard title="Calendar Preferences">
            {error && <p className="p-4 text-sm text-destructive">{error}</p>}
            {loadError && (
              <div className="flex flex-wrap items-center justify-between gap-3 p-4 text-sm text-muted-foreground">
                <span>{loadError}</span>
                <Button type="button" size="sm" variant="outline" onClick={() => { retryCountRef.current = 0; setLoadError(''); void loadCalendarData(); }}>Retry</Button>
              </div>
            )}
            <div className="divide-y divide-border">
              <div className="flex items-center justify-between p-4">
                <div>
                  <h4 className="font-semibold text-sm">Hide Weekends</h4>
                  <p className="text-xs text-muted-foreground">Do not show weekends in calendar views.</p>
                </div>
                <Switch checked={prefs.hide_weekends} onCheckedChange={(checked) => updatePrefs({...prefs, hide_weekends: checked})} />
              </div>

              <div className="flex items-center justify-between p-4">
                <div>
                  <h4 className="font-semibold text-sm">Week Start</h4>
                  <p className="text-xs text-muted-foreground">Select the day that starts the week.</p>
                </div>
                <Select value={String(prefs.week_start)} onValueChange={(val) => updatePrefs({...prefs, week_start: Number(val)})}>
                  <SelectTrigger className="w-[180px]">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="0">Sunday</SelectItem>
                    <SelectItem value="1">Monday</SelectItem>
                  </SelectContent>
                </Select>
              </div>

              <div className="flex items-center justify-between p-4">
                <div>
                  <h4 className="font-semibold text-sm">Default Reminder</h4>
                  <p className="text-xs text-muted-foreground">Default notification reminder offset.</p>
                </div>
                <Select value={String(prefs.default_reminder_minutes)} onValueChange={(val) => updatePrefs({...prefs, default_reminder_minutes: Number(val)})}>
                  <SelectTrigger className="w-[180px]">
                    <SelectValue />
                  </SelectTrigger>
                  <SelectContent>
                    <SelectItem value="5">5 minutes before</SelectItem>
                    <SelectItem value="15">15 minutes before</SelectItem>
                    <SelectItem value="30">30 minutes before</SelectItem>
                    <SelectItem value="60">1 hour before</SelectItem>
                  </SelectContent>
                </Select>
              </div>
            </div>
          </SectionCard>

          <SectionCard title="Calendars">
            <div className="divide-y divide-border">
              {calendars.map((cal) => (
                <div key={cal.id} className="flex items-center justify-between p-4">
                  <div>
                    <h4 className="font-semibold text-sm">{cal.name}</h4>
                  </div>
                  <Switch checked={cal.visible} onCheckedChange={() => toggleCalendarVisibility(cal.id, cal.visible)} />
                </div>
              ))}
              {calendars.length === 0 && (
                <p className="p-4 text-center text-sm text-muted-foreground">No accounts calendars registered.</p>
              )}
            </div>
          </SectionCard>
        </div>
      )}
    </PaneShell>
  );
}
