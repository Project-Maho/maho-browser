# Maho Launch Day Runbook

**D-Day**: `<YYYY-MM-DD>` — fill this in when D0.6 is anchored.

## Prerequisites

Green ALL of these before starting the runbook.

- [ ] Phase 1 shipped (website domain migration on `mahobrowser.com`) — verified via `curl -sI https://mahobrowser.com/` returns 200.
- [ ] Phase 2 relay production deployed at `relay.mahobrowser.com` — verified via `curl https://relay.mahobrowser.com/health` returns `ok`.
- [ ] All 5 Custom Domain subdomains bound (`relay`, `sync`, `update`, `updates`, `proxy`) — `wrangler deploy --env production` output listed them.
- [ ] `PUBLIC_RELEASE_LIVE=false` currently in effect on the website (default) — `curl -s https://mahobrowser.com/download | grep -c "meta http-equiv=\"refresh\""` returns 0.
- [ ] `Project-Maho/release` GitHub repo exists (private or public) with the DMG artifact + SHA256 checksums.
- [ ] DMG signed + notarized: `spctl -a -vv Maho.dmg` shows "accepted".
- [ ] `LS_WEBHOOK_SECRET` set in production Worker: `bunx wrangler secret list --env production` includes it.
- [ ] LemonSqueezy dashboard webhook URL points at `https://relay.mahobrowser.com/billing/lemonsqueezy`.
- [ ] DMARC upgraded to `p=quarantine` (or `p=reject`).
- [ ] Discord invite active at `https://discord.gg/Ty4J6pMdqg`.

## T-24h — Freeze + rehearsal

1. Post to Discord: "Launch tomorrow."
2. Dry-run the T-0 build without deploying:
   ```bash
   cd /Users/indo/code/project/maho-workspace/website
   PUBLIC_RELEASE_LIVE=false SITE_URL=https://mahobrowser.com \
   PUBLIC_RELAY_URL=https://relay.mahobrowser.com \
   PUBLIC_RELEASE_REPO_URL=https://github.com/Project-Maho/release \
   bun run build
   grep -c 'meta http-equiv="refresh"' dist/download/index.html
   ```
   Expect `0` (pre-launch state).

3. Flip locally + verify:
   ```bash
   PUBLIC_RELEASE_LIVE=true (rest same) bun run build
   grep -c 'meta http-equiv="refresh"' dist/download/index.html
   ```
   Expect `1`.

4. Do NOT deploy yet — revert to `PUBLIC_RELEASE_LIVE=false` and clean the build.

## T-0 — Launch

1. Publish GitHub release (make repo public if it was private).

2. Verify DMG downloads and hash matches:
   ```bash
   curl -LO https://github.com/Project-Maho/release/releases/latest/download/Maho.dmg
   shasum -a 256 Maho.dmg
   ```
   Compare against published SHA in release notes.

3. Flip website release gate and redeploy:
   ```bash
   cd /Users/indo/code/project/maho-workspace/website
   SITE_URL=https://mahobrowser.com \
   PUBLIC_RELAY_URL=https://relay.mahobrowser.com \
   PUBLIC_RELEASE_LIVE=true \
   PUBLIC_RELEASE_REPO_URL=https://github.com/Project-Maho/release \
   bun run build

   bunx wrangler pages deploy dist \
     --project-name=maho-website \
     --branch=main \
     --commit-dirty=true
   ```

4. Smoke test:
   ```bash
   curl -sI https://mahobrowser.com/download
   ```
   Expect `HTTP/2 200` + `refresh` meta redirect in body.

5. Post launch announcement:
   - Discord (`https://discord.gg/Ty4J6pMdqg`)
   - X (`@mahobrowser` — TBD)
   - Hacker News (Show HN)

## T+1h — Monitor

- Watch Cloudflare Workers dashboard for `maho-relay-production` — check request rate + errors.
- Watch D1 dashboard for `maho-relay-db` — check writes + errors.
- Watch DMARC reports in `hello@mahobrowser.com` inbox.
- Read Discord + Twitter for user reports.

## T+24h → T+7d

- Daily: check relay errors, billing webhook events (`processed_events` D1 table growth), waitlist signup rate.
- Respond to `hello@mahobrowser.com` within same business day.
- Draft D+7 changelog post.

## Rollback

If a critical issue lands within T+1h:

Website:
```bash
cd website
PUBLIC_RELEASE_LIVE=false (rest same) bun run build
bunx wrangler pages deploy dist --project-name=maho-website --branch=main --commit-dirty=true
```

Or Cloudflare Pages Dashboard → project → Deployments → "Rollback to previous deployment".

Relay:
```bash
cd maho/relay
bunx wrangler rollback --env production
```

DMARC (if false positives spike):
```bash
# Downgrade to p=none monitoring
export CLOUDFLARE_API_TOKEN='<scope: Zone:DNS:Edit>'
ZONE=af1d3367cb526a8eb008b58078cd9185
DMARC_ID=$(curl -s -H "Authorization: Bearer $CLOUDFLARE_API_TOKEN" \
  "https://api.cloudflare.com/client/v4/zones/$ZONE/dns_records?type=TXT&name=_dmarc.mahobrowser.com" \
  | python3 -c "import sys,json; print(json.load(sys.stdin)['result'][0]['id'])")
curl -X PATCH -H "Authorization: Bearer $CLOUDFLARE_API_TOKEN" -H "Content-Type: application/json" \
  "https://api.cloudflare.com/client/v4/zones/$ZONE/dns_records/$DMARC_ID" \
  -d '{"content":"\"v=DMARC1; p=none; rua=mailto:hello@mahobrowser.com; ruf=mailto:hello@mahobrowser.com; fo=1; adkim=r; aspf=r; pct=100\""}'
```
