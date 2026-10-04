// Copyright 2026 Maho Browser. All rights reserved.

import React, {useCallback, useEffect, useRef, useState} from 'react';
import {Loader2} from 'lucide-react';
import {Button} from '@ui/button';
import {Switch} from '@ui/switch';
import {Select, SelectContent, SelectItem, SelectTrigger, SelectValue} from '@ui/select';
import {PaneShell, SectionCard} from './domain_panes.js';
import {classifyMailSettingsFailure, type MailSettingsState} from './mail_settings_state.js';
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

// Mirrors the rows seeded by the mail-core migration (calendar.*), which is
// also what the Mail calendar falls back to, so Settings and the calendar
// agree even before the first write.
export const DEFAULT_CALENDAR_PREFS: CalendarPrefs = {
  hide_weekends: false,
  week_start: 0,
  working_hours_start: '09:00',
  working_hours_end: '18:00',
  default_reminder_minutes: 10,
};

// The page handler always answers with a complete object, but a failed or
// stale backend can still put `null`, a bare string, or a partial dict on the
// wire. Normalizing at this boundary keeps pane state a real CalendarPrefs:
// reading `hide_weekends` off a null payload threw during render and tore down
// the whole settings app.
export function normalizeCalendarPrefs(prefsJson: string): CalendarPrefs {
  let parsed: unknown;
  try {
    parsed = JSON.parse(prefsJson);
  } catch {
    return DEFAULT_CALENDAR_PREFS;
  }
  if (!parsed || typeof parsed !== 'object' || Array.isArray(parsed)) {
    return DEFAULT_CALENDAR_PREFS;
  }
  const source = parsed as Record<string, unknown>;
  return {
    hide_weekends: typeof source.hide_weekends === 'boolean' ?
        source.hide_weekends :
        DEFAULT_CALENDAR_PREFS.hide_weekends,
    week_start: typeof source.week_start === 'number' ?
        source.week_start :
        DEFAULT_CALENDAR_PREFS.week_start,
    working_hours_start: typeof source.working_hours_start === 'string' ?
        source.working_hours_start :
        DEFAULT_CALENDAR_PREFS.working_hours_start,
    working_hours_end: typeof source.working_hours_end === 'string' ?
        source.working_hours_end :
        DEFAULT_CALENDAR_PREFS.working_hours_end,
    default_reminder_minutes:
        typeof source.default_reminder_minutes === 'number' ?
        source.default_reminder_minutes :
        DEFAULT_CALENDAR_PREFS.default_reminder_minutes,
  };
}

export function MailCalendarPane({pane, store}: {pane: PaneDefinition; store: MahoSettingsStore}) {
  const handler = store.getHandler();
  const router = store.getCallbackRouter();
  const mountedRef = useRef(true);

  const [prefs, setPrefs] = useState<CalendarPrefs>(DEFAULT_CALENDAR_PREFS);
  const [categories, setCategories] = useState<CalendarCategory[]>([]);
  const [calendars, setCalendars] = useState<AccountCalendar[]>([]);
  const [modelState, setModelState] = useState<MailSettingsState<CalendarPrefs>>({status: 'loading'});
  const [error, setError] = useState('');
  useEffect(() => {
    return () => {
      mountedRef.current = false;
    };
  }, []);

  const loadCalendarData = useCallback(async () => {
    setModelState({status: 'loading'});
    try {
      const {ok: okPrefs, resultJson: prefsJson} = await handler.mailGetCalendarPrefs();
      if (!mountedRef.current) return;
      if (!okPrefs) {
        setModelState(classifyMailSettingsFailure(prefsJson, 'Failed to load calendar settings'));
        return;
      }
      const nextPrefs = normalizeCalendarPrefs(prefsJson);
      setPrefs(nextPrefs);

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
        setModelState({status: 'ready', data: nextPrefs});
      }
    } catch {
      if (!mountedRef.current) return;
      setModelState({status: 'error', message: 'Failed to load calendar settings'});
    }
  }, [handler]);

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
    const previous = prefs;
    setPrefs(newPrefs);
    setError('');
    try {
      const {ok} = await handler.mailSetCalendarPrefs(JSON.stringify(newPrefs));
      if (!ok) {
        setPrefs(previous);
        setError('Failed to update calendar preferences');
      }
    } catch {
      if (mountedRef.current) {
        setPrefs(previous);
        setError('Failed to update calendar preferences');
      }
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
      <div data-mail-settings-state={modelState.status}>
      {modelState.status === 'loading' ? (
        <p className="py-4 text-center text-sm text-muted-foreground"><Loader2 className="mr-2 inline size-4 animate-spin" />Loading calendar settings...</p>
      ) : modelState.status === 'unavailable' || modelState.status === 'error' ? (
        <div className="flex flex-wrap items-center justify-between gap-3 p-4 text-sm text-muted-foreground"><span>{modelState.message}</span><Button type="button" size="sm" variant="outline" onClick={() => { void loadCalendarData(); }}>Retry</Button></div>
      ) : (
        <div className="space-y-6">
          <SectionCard title="Calendar Preferences">
            {error && <p className="p-4 text-sm text-destructive">{error}</p>}

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
                    <SelectItem value="10">10 minutes before</SelectItem>
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
      </div>
    </PaneShell>
  );
}
