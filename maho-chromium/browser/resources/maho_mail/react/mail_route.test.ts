import {describe, expect, it} from 'vitest';

import {parseMailRoute} from './mail_route';

describe('Mail command routes', () => {
  it.each([
    ['?view=compose', {kind: 'compose'}],
    ['?view=search', {kind: 'search'}],
    ['?view=unread', {kind: 'synthetic', view: 'unread'}],
    ['?view=snoozed', {kind: 'synthetic', view: 'snoozed'}],
    ['?folder=inbox', {kind: 'folder', folder: 'Inbox'}],
    ['?folder=sent', {kind: 'folder', folder: 'Sent'}],
    ['?folder=drafts', {kind: 'folder', folder: 'Drafts'}],
    ['?folder=starred', {kind: 'synthetic', view: 'starred'}],
  ])('parses %s', (search, expected) => {
    expect(parseMailRoute(search)).toEqual(expected);
  });

  it('rejects unknown routes', () => {
    expect(parseMailRoute('?folder=trash')).toEqual({kind: 'none'});
  });
});
