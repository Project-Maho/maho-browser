// re(?:\[\d+\])? — Re, RE, re, Re[2], Re[42]
// fwd?           — Fwd, FWD, fwd, FW, fw
// 转发|转         — Chinese forward
// 회신|답장|전달   — Korean reply / forward
// 返信|転送        — Japanese reply / forward
const PREFIX_RE =
  /^(?:re(?:\[\d+\])?|fwd?|转发|转|회신|답장|전달|返信|転送)\s*:\s*/i;

function stripPrefixes(subj: string): string {
  let s = subj.trim();
  let prev: string;
  do {
    prev = s;
    s = s.replace(PREFIX_RE, "").trim();
  } while (s !== prev);
  return s;
}

export function buildReplySubject(subj: string): string {
  return `Re: ${stripPrefixes(subj)}`;
}

export function buildForwardSubject(subj: string): string {
  return `Fwd: ${stripPrefixes(subj)}`;
}
