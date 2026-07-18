# maho-ops

Maho Browser 커뮤니티 운영 자동화. Discord ↔ GitHub Issue 브릿지 + Hermes cron 발송기.

## 구성

- `bot.py` — Discord 봇. Forum 채널(`#bug-report`, `#feedback`) 새 스레드 감지 → LLM 분석 → GitHub private issue 생성 → 스레드에 접수 답신
- `llm.py` — LLM 호출 (Quotio local gateway = OpenAI-compatible, gpt-5.5).
- `github_ops.py` — GitHub API wrapper (gh CLI 재활용)
- `mapping.db` — SQLite. `discord_thread_id ↔ github_issue_number ↔ status`
- `release_watcher.py` — GitHub release published 감지 → 관련 스레드에 코멘트 (Hermes cron이 호출)
- `.env` — 봇 토큰, 채널 ID (gitignore)
- Quotio 로컬 게이트웨이(`http://127.0.0.1:8317/v1`)가 켜져 있어야 함 — 별도 API 키/크레딧 관리 X
- `Makefile` — 자주 쓰는 명령 shortcut

## 채널 매핑

| 역할 | Discord | Type | ID |
|---|---|---|---|
| Bug reports | `#bug-report` | Forum | `1527678813000171661` |
| Feature requests | `#feedback` | Forum | `1525536503894118420` |
| Release announcements | `#announcements` | Text | `1525536100691607593` |
| GTM/dev 리마인더 | `#moderator-only` | Text | `1525538067258277942` |
| Guild | Maho Browser | - | `1524553839661944842` |

## GitHub 저장소

- Issues: `Project-Maho/maho-browser` (private)
- Releases: `Project-Maho/release` (public, 바이너리 전용)

## 실행

```bash
# 첫 세팅
make setup                  # venv 만들고 의존성 설치

# 로컬 테스트
make dev                    # foreground 실행

# 서비스 등록 (macOS)
make install-service        # launchd plist 등록
make start-service          # 서비스 시작
make stop-service           # 서비스 중지
make logs                   # tail 로그
```

## 안전 규칙 (maho anti-패턴)

- GitHub 이슈 URL을 유저에게 노출하지 않는다 (Discord 답신에 링크 X)
- 봇 답신은 접수 확인만; 로드맵/일정 공개 금지
- 자동 close 없음 (사람이 결정)
- 상표 노출 없음 (경쟁사 이름 자동 삽입 X)
