export type CommandCategory =
  | 'mail'
  | 'compose'
  | 'navigate'
  | 'search'
  | 'view'
  | 'settings'
  | 'ai';

export interface PaletteCommand {
  id: string;
  label: string;
  shortcut?: string;
  category: CommandCategory;
  keywords?: string[];
  handler: () => void;
  enabled?: () => boolean;
}

interface ScoredCommand {
  command: PaletteCommand;
  score: number;
}

const SCORE_PREFIX_MATCH = 100;
const SCORE_WORD_START_MATCH = 50;
const SCORE_SUBSTRING_MATCH = 20;
const SCORE_KEYWORD_MATCH = 10;
const SCORE_CATEGORY_MATCH = 5;

export function calculateScore(command: PaletteCommand, terms: string[]): number {
  let score = 0;
  const labelLower = command.label.toLowerCase();
  const categoryLower = command.category.toLowerCase();
  const keywordsLower = command.keywords?.map((k) => k.toLowerCase()) ?? [];

  for (const term of terms) {
    if (term.length === 0) continue;

    if (labelLower.startsWith(term)) {
      score += SCORE_PREFIX_MATCH;
    } else if (labelLower.includes(` ${term}`) || labelLower.includes(`-${term}`)) {
      score += SCORE_WORD_START_MATCH;
    } else if (labelLower.includes(term)) {
      score += SCORE_SUBSTRING_MATCH;
    }

    for (const keyword of keywordsLower) {
      if (keyword.includes(term)) {
        score += SCORE_KEYWORD_MATCH;
        break;
      }
    }

    if (categoryLower.includes(term)) {
      score += SCORE_CATEGORY_MATCH;
    }
  }

  return score;
}

export function groupByCategory(commands: PaletteCommand[]): PaletteCommand[] {
  const groups: Record<CommandCategory, PaletteCommand[]> = {
    mail: [],
    compose: [],
    navigate: [],
    search: [],
    view: [],
    settings: [],
    ai: [],
  };

  for (const cmd of commands) {
    groups[cmd.category].push(cmd);
  }

  const result: PaletteCommand[] = [];
  const categoryOrder: CommandCategory[] = ['mail', 'compose', 'navigate', 'search', 'view', 'ai', 'settings'];

  for (const category of categoryOrder) {
    result.push(...groups[category]);
  }

  return result;
}

export function useCommandRegistry(commands: PaletteCommand[]) {
  const search = (query: string): PaletteCommand[] => {
    const trimmedQuery = query.trim().toLowerCase();

    if (trimmedQuery.length === 0) {
      const enabledCommands = commands.filter(
        (cmd) => cmd.enabled === undefined || cmd.enabled()
      );
      return groupByCategory(enabledCommands).slice(0, 15);
    }

    const terms = trimmedQuery.split(/\s+/).filter((t) => t.length > 0);
    const scored: ScoredCommand[] = [];

    for (const command of commands) {
      if (command.enabled !== undefined && !command.enabled()) {
        continue;
      }

      const score = calculateScore(command, terms);
      if (score > 0) {
        scored.push({ command, score });
      }
    }

    scored.sort((a, b) => b.score - a.score);

    return scored.slice(0, 15).map((s) => s.command);
  };

  return { search };
}
