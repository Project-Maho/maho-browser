# Maho Browser Rust Backend 성능 분석 보고서

## Executive Summary

본 보고서는 Maho Browser monorepo의 Rust backend를 정적·읽기 전용으로 점검한 결과입니다. 분석 범위는 `maho/crates/*`의 핵심 crate(`maho-core`, `maho-ffi`, `maho-storage`, `maho-types`, `maho-engine`, `maho-import`, `maho-agent`, `maho-browser-mcp`, `maho-cli`, `maho-jni`), 독립 workspace인 `maho/relay`, 그리고 존재가 확인된 `maho/mail-core`입니다. 소스 파일은 수정하지 않았고 `cargo build/test/check`는 실행하지 않았습니다.

가장 큰 성능 위험은 **Chromium UI thread에서 MahoCore 초기화가 동기적으로 진행되는 구조**입니다. `MahoBrowserMainExtraParts::OnRegularStorageDirectoryReady()`는 UI thread에서 `CreateWithStorage()`와 `LoadState()`를 직접 호출합니다. Rust 측 `maho_core_new_with_storage()`는 SQLite/LMDB를 열고 스키마 초기화·마이그레이션, content blocker hydrate/rebuild, shortcut/download 복원, 전체 memory embedding 로드와 HNSW 재구축까지 수행합니다. 이후 `LoadState()`가 LMDB의 탭·스페이스·프로필·계정과 다수 SQLite-backed manager 데이터를 다시 복원합니다. 따라서 사용자 데이터가 증가할수록 브라우저 시작 지연과 UI thread stall이 비례해 커질 가능성이 높습니다.

두 번째 핵심 문제는 startup hydrate 과정에서의 **불필요한 전체 스캔 및 N+1 접근**입니다. memory index는 매 시작마다 `memory_facts`의 embedding BLOB 전체를 읽어 HNSW를 새로 만들고, boost 복원은 모든 boost에 대해 `domain_boost_state`를 다시 조회합니다. 또한 split-view 복원은 pane 하나마다 전체 tab 목록을 재순회하고 RFC3339 날짜 파싱까지 반복합니다.

세 번째 범주는 **FFI 경계의 반복 JSON 직렬화와 clone fan-out**입니다. Core update는 listener 수만큼 `CoreUpdate`를 clone하고, C++ callback 전달 전에 다시 JSON 문자열과 `CString`을 생성합니다. update 빈도가 높은 UI 상태에서 allocation과 memcpy 비용이 누적될 수 있습니다.

`maho/mail-core`에서는 async task 안에서 synchronous SQLite·filesystem 작업이 수행되고, cache eviction조차 `tokio::spawn(async move { ... })` 내부에서 동기 `read_dir`/metadata/sort/remove를 실행합니다. `maho/relay`에서는 Google ID token 검증마다 JWKS를 네트워크에서 fetch하여 인증 latency와 외부 호출 수가 증가하는 구조가 확인되었습니다.

우선순위는 다음과 같습니다.

1. Chromium UI thread에서 수행되는 MahoCore startup hydrate를 단계적으로 분리하고 background thread/task로 이관합니다.
2. memory HNSW index를 매 startup마다 전량 재구축하지 않도록 persistent snapshot 또는 증분 index를 도입합니다.
3. startup N+1 및 O(n×m) 복원 경로를 단일 쿼리/사전 인덱싱으로 교체합니다.
4. FFI hot update는 typed/binary ABI 또는 최소 payload로 줄이고 callback registry clone을 피합니다.
5. mail-core의 blocking I/O를 `spawn_blocking` 또는 async I/O로 이동하고 relay JWKS cache를 추가합니다.

## Severity Table

| ID | Severity | 영역 | 요약 |
|---|---|---|---|
| RUST-01 | High | Startup / maho-core | Chromium UI thread에서 SQLite/LMDB 초기화, hydrate, HNSW rebuild, LoadState가 동기 실행됩니다. |
| RUST-02 | High | maho-core / maho-storage | 전체 memory embedding BLOB을 매 startup마다 읽고 HNSW를 전량 재구축합니다. |
| RUST-03 | Medium | maho-core / maho-storage | boost 복원 시 전체 boost마다 active boost를 재조회하는 N+1 쿼리가 있습니다. |
| RUST-04 | Medium | maho-storage | DB open 때마다 schema initialization 뒤 다수 `ALTER TABLE` migration을 무조건 다시 시도합니다. |
| RUST-05 | Medium | maho-core | split-view restore가 pane마다 전체 tab을 순회하고 RFC3339 파싱을 반복해 O(panes×tabs)입니다. |
| RUST-06 | Medium | maho-core / maho-ffi | CoreUpdate가 listener별 clone되고 FFI callback마다 JSON/CString allocation을 수행합니다. |
| RUST-07 | Low | maho-core | chat session과 memory manager가 서로 독립적인 global Tokio runtime을 생성합니다. |
| RUST-08 | Medium | maho/mail-core | async attachment 처리 안에서 synchronous SQLite/filesystem I/O가 실행됩니다. |
| RUST-09 | Medium | maho/mail-core | cache eviction을 async task로 spawn하지만 내부 작업은 전부 동기 filesystem scan/sort/delete입니다. |
| RUST-10 | Medium | maho/relay | Google ID token 검증마다 JWKS를 fetch하며 cache가 없습니다. |
| RUST-11 | Low | maho-core | semantic search마다 query embedding을 새 `Vec<f32>`로 복사하고 결과 ID도 clone합니다. |

---

## Prioritized Findings

### RUST-01 — High — Chromium UI thread에서 MahoCore startup 전체가 동기 실행됩니다

**근거**

- `maho-chromium/browser/maho_browser_main_extra_parts.cc:3984-4027`
  - `DCHECK_CURRENTLY_ON(content::BrowserThread::UI)`로 UI thread임을 명시합니다.
  - 같은 함수에서 `DeriveAndSetStorageKey(...)`, `CreateWithStorage(...)`, `LoadState(core_)`를 순차적으로 직접 호출합니다.
- `maho/crates/maho-ffi/src/lib.rs:3744-3775`
  - `maho_core_new_with_storage()`가 `create_dir_all`, `MahoCore::new()`, `.with_storage(...)`, `.with_lmdb_storage(...)`, `load_skills_from_dir(...)`를 동기 호출합니다.
- `maho/crates/maho-core/src/maho_core.rs:2655-2760`
  - `with_storage()`에서 SQLite open 이후 settings, content blocker state/list/site exception, shortcut, download를 즉시 복원합니다.
  - cache miss 시 `content_blocker.rebuild_engine_sync()`가 startup path에서 직접 실행됩니다.
  - 마지막에는 `memory_manager.rebuild_index(storage)`를 동기 호출합니다.
- `maho/crates/maho-core/src/maho_core.rs:3004-3340`
  - `load_state()`가 LMDB migration, 탭/스페이스/프로필/account 복원과 `load_persisted_data()`를 같은 호출 스택에서 수행합니다.

**성능 영향**

브라우저 startup의 UI thread가 데이터 크기와 디스크 상태에 직접 노출됩니다. SQLCipher open, filesystem access, schema DDL, LMDB read, serde deserialize, content blocker rebuild, memory HNSW build가 누적되면 first window responsiveness와 startup time이 악화될 수 있습니다. 특히 사용자 profile이 커질수록 비용이 선형 또는 그 이상으로 증가합니다.

**권장 수정**

- `CreateWithStorage()`를 최소한의 core object 및 DB handle 준비만 수행하도록 축소합니다.
- settings처럼 first paint에 필요한 소량 상태만 동기 hydrate하고, downloads/history/notes/boosts/memory index/skills/content blocker rebuild는 sequenced background task로 지연합니다.
- C++에서는 `PostTaskAndReplyWithResult` 또는 전용 sequenced task runner를 사용하여 Rust startup hydrate를 UI thread 밖에서 수행하고, UI thread에는 readiness state만 전달합니다.
- content blocker는 serialized engine cache가 invalid할 경우 startup을 막지 말고 이전 안전 상태로 시작한 뒤 background rebuild 후 atomic swap하도록 변경하는 것이 좋습니다.

---

### RUST-02 — High — 전체 memory embedding을 startup마다 full scan하고 HNSW를 전량 재구축합니다

**근거**

- `maho/crates/maho-core/src/maho_core.rs:2749-2758`
  - startup `with_storage()` 마지막에 `self.memory_manager.rebuild_index(storage)`가 동기 호출됩니다.
- `maho/crates/maho-core/src/memory_manager.rs:121-145`
  - `rebuild_index()`가 `load_all_active_memory_embeddings()` 결과를 전부 메모리에 올립니다.
  - `(id, bytes)` 전체를 `Vec<String>`/`Vec<Embedding>`으로 다시 적재하고 `Builder::default().build(vecs, ids)`로 HNSW 전체를 새로 만듭니다.
- `maho/crates/maho-storage/src/sqlite.rs:4192-4207`
  - `SELECT id, embedding FROM memory_facts WHERE embedding IS NOT NULL`로 embedding BLOB 전량을 반환합니다.
- `maho/crates/maho-storage/src/sqlite.rs:1415-1434`
  - `memory_facts`에는 `embedding BLOB`이 있으나 해당 startup lookup을 줄여주는 별도 상태/index snapshot은 없습니다.

**성능 영향**

memory fact 수와 embedding dimension이 증가할수록 startup DB read, BLOB copy, byte→float 변환, heap allocation, HNSW construction 비용이 모두 증가합니다. 이 작업이 RUST-01의 UI-thread startup path 안에 있어 체감 지연으로 직결됩니다.

**권장 수정**

- HNSW를 별도 persistent artifact로 snapshot하고 정상 종료 또는 batch update 시 증분 갱신합니다.
- DB에는 `memory_index_version`, embedding model/version, last indexed row/version을 저장해 변경분만 반영합니다.
- full rebuild가 필요한 경우 background `spawn_blocking`/worker thread에서 수행하고, 준비 전에는 FTS-only fallback을 사용합니다.
- `Vec<(String, Vec<u8>)>` 전체 materialization 대신 cursor/iterator 기반 chunk loading을 사용하여 peak memory를 낮춥니다.

---

### RUST-03 — Medium — boost startup restore에 N+1 SQLite query가 있습니다

**근거**

- `maho/crates/maho-core/src/maho_core.rs:3325-3337`
  - `storage.load_boosts()`로 boost 목록 전체를 읽은 뒤, 각 boost마다 `storage.get_active_boost(&domain)`을 호출합니다.
- `maho/crates/maho-storage/src/sqlite.rs:2155-2165`
  - `get_active_boost()`는 매 호출마다 `SELECT active_boost_id FROM domain_boost_state WHERE domain = ?1` statement를 prepare하고 query합니다.

**성능 영향**

boost 수를 N이라고 하면 startup DB round-trip/prepare가 1+N번 발생합니다. local SQLite라 네트워크 N+1보다는 저렴하지만, SQLCipher와 startup hot path라는 점을 고려하면 불필요한 비용입니다.

**권장 수정**

- `load_boosts_with_active_state()` 형태로 `boosts`와 `domain_boost_state`를 `LEFT JOIN`하여 한 번에 읽습니다.
- 또는 `domain_boost_state` 전체를 한 번 읽어 `HashMap<domain, active_boost_id>`으로 만든 뒤 boost restore loop에서 O(1) lookup합니다.
- startup hydrate API를 domain별 manager 단위의 bulk read 형태로 통일하는 것이 좋습니다.

---

### RUST-04 — Medium — SQLite open마다 schema DDL/migration을 반복 실행합니다

**근거**

- `maho/crates/maho-storage/src/sqlite.rs:495-513`
  - `open_with_key()`는 매 open마다 `initialize_schema()`와 `run_migrations()`를 호출합니다.
- `maho/crates/maho-storage/src/sqlite.rs:539-565` 부근
  - `run_migrations()`는 `ALTER TABLE shortcuts ADD COLUMN ...`, `ALTER TABLE downloads ADD COLUMN ...`, `ALTER TABLE atc_rules ADD COLUMN ...` 등 이미 적용되었을 가능성이 높은 migration DDL을 다시 실행하고 에러를 무시하는 패턴을 사용합니다.

**성능 영향**

매 startup마다 SQLite parser/schema lock/metadata lookup과 실패하는 DDL 경로를 반복하게 됩니다. migration 수가 누적될수록 cold startup 비용이 계속 증가합니다. SQLCipher DB에서는 page/schema access 자체도 일반 SQLite보다 비용이 큽니다.

**권장 수정**

- `PRAGMA user_version` 또는 `schema_migrations(version PRIMARY KEY)`를 사용한 명시적 migration versioning으로 전환합니다.
- 적용되지 않은 migration만 transaction 하나에서 순서대로 실행합니다.
- `initialize_schema()`의 `CREATE TABLE/INDEX IF NOT EXISTS`도 bootstrap DB와 migration DB를 구분해 최소화합니다.

---

### RUST-05 — Medium — split-view restore가 O(panes × tabs)이며 날짜 파싱을 중첩 반복합니다

**근거**

- `maho/crates/maho-core/src/maho_core.rs:3204-3233`
  - 모든 persisted window를 순회합니다.
  - 각 `stable_pane`마다 `tab_manager.get_all_tabs()` 전체를 순회합니다.
  - 각 tab마다 `chrono::DateTime::parse_from_rfc3339(&t.created_at.0)`를 다시 실행합니다.
  - matching tab을 `Vec<&Tab>`로 collect하여 추가 allocation도 발생합니다.

**성능 영향**

pane 수 P, tab 수 T일 때 O(P×T) 비교와 O(P×T) 날짜 파싱이 발생합니다. tab/session restore 직후 startup path에서 실행되므로 대량 탭 복원 profile에서 비용이 커질 수 있습니다.

**권장 수정**

- 복원 전에 `(url, creation_time_micros) -> tab_id` 인덱스를 한 번 `HashMap`으로 구축하여 O(T+P)로 줄입니다.
- creation timestamp는 tab 생성 시/deserialize 시 integer micros를 함께 유지해 RFC3339 반복 parsing을 제거합니다.
- ambiguity 확인이 필요하면 value를 단일 ID가 아니라 `SmallVec<[TabId; 1]>` 또는 count+first-id 형태로 저장합니다.

---

### RUST-06 — Medium — CoreUpdate fan-out에서 clone과 JSON/CString allocation이 중첩됩니다

**근거**

- `maho/crates/maho-core/src/maho_core.rs:2331-2341`
  - tab callback이 update를 `emitted_during_dispatch`에 넣기 위해 `update.clone()`하고, 각 listener 호출마다 다시 `listener(update.clone())`합니다.
- `maho/crates/maho-ffi/src/lib.rs:15045-15110`
  - FFI callback registry를 `Mutex<Vec<...>>`에서 lock한 뒤 `callbacks.clone()`으로 전체 callback metadata를 복사합니다.
  - Split/Tool update마다 `serde_json::to_string(update)`와 `CString::new(json_str)`을 새로 할당합니다.
  - Settings update도 전체 `settings` payload를 JSON으로 직렬화합니다.
- `maho/crates/maho-ffi/src/lib.rs:233-239`
  - 일반 FFI 반환도 `serde_json::to_string` → `CString` → `into_raw` 단계를 거칩니다.

**성능 영향**

UI update 빈도가 높고 payload가 클수록 clone, serialization, temporary string allocation과 memcpy가 누적됩니다. listener 수가 증가하면 Rust 내부 clone 비용도 선형 증가합니다.

**권장 수정**

- 내부 listener API를 `Fn(&CoreUpdate)` 또는 `Arc<CoreUpdate>` 기반으로 바꾸어 payload clone을 제거합니다.
- callback registry는 `Arc<[CallbackEntry]>` snapshot 또는 read-optimized registry를 사용하여 매 dispatch마다 전체 `Vec` clone을 피합니다.
- high-frequency event는 JSON 대신 stable C ABI struct, tagged union, FlatBuffers/MessagePack 등 typed/binary transport를 고려합니다.
- SettingsChanged는 전체 settings가 아니라 변경된 field mask/delta만 전송합니다.

---

### RUST-07 — Low — maho-core 내부에 별도 Tokio runtime이 둘 존재합니다

**근거**

- `maho/crates/maho-core/src/memory_manager.rs:14-17`
  - `OnceLock<Runtime>`에서 `Runtime::new()`로 shared memory runtime을 생성합니다.
- `maho/crates/maho-core/src/chat_session.rs:247-249`
  - 별도의 `OnceLock<Runtime>`에서 다시 `Runtime::new()`를 생성합니다.

**성능 영향**

`Runtime::new()`는 multi-thread Tokio runtime을 생성하므로 thread pool, scheduler, timer/IO driver가 중복됩니다. 둘 다 lazy이므로 항상 startup cost가 발생하는 것은 아니지만, AI/chat와 memory가 함께 사용되면 프로세스 thread/memory footprint가 불필요하게 늘어납니다.

**권장 수정**

- `maho-core` 전역 runtime provider를 하나만 두고 두 subsystem이 같은 `Handle`을 공유합니다.
- Chromium embedding 환경에서는 가능하면 C++/process-level executor 정책과 맞춘 dedicated runtime 1개를 명시적으로 lifecycle 관리합니다.

---

### RUST-08 — Medium — mail-core attachment async path가 synchronous DB/filesystem 작업으로 executor를 차단합니다

**근거**

- `maho/mail-core/src/ffi/compose_api.rs:1047-1077`
  - `Box::pin(async move { ... })` 내부에서 synchronous pool `ctx.pool.get()`, `std::fs::create_dir_all`, `file_path.exists()`, synchronous `rusqlite::query_row`를 실행합니다.
- `maho/mail-core/src/ffi/compose_api.rs:1128-1130`
  - attachment fetch 자체는 `spawn_blocking`으로 분리했지만 결과 파일 저장은 async task 안에서 `std::fs::write`로 직접 수행합니다.

**성능 영향**

filesystem이 느리거나 SQLite pool이 대기 중일 때 Tokio worker thread를 점유합니다. 동시 attachment download가 많아지면 unrelated mail async work의 latency가 증가할 수 있습니다.

**권장 수정**

- synchronous rusqlite 접근은 DB 전용 blocking executor 또는 `spawn_blocking`으로 묶습니다.
- `create_dir_all`, existence check, write는 `tokio::fs` 또는 하나의 `spawn_blocking` 작업으로 이동합니다.
- attachment cache hit check와 DB/auth resolution을 가능하면 I/O batch 하나로 묶어 task scheduling overhead를 줄입니다.

---

### RUST-09 — Medium — mail-core cache eviction을 spawn하지만 실제 작업은 전부 blocking입니다

**근거**

- `maho/mail-core/src/ffi/compose_api.rs:340-389`
  - `enforce_cache_limit()`가 `std::fs::read_dir`, 각 entry `metadata()`, 전체 `Vec` 수집, modified-time sort, `remove_file`을 동기 실행합니다.
- `maho/mail-core/src/ffi/compose_api.rs:1132-1135`
  - 이를 `tokio::spawn(async move { enforce_cache_limit(...) })`로 실행합니다. async wrapper만 있을 뿐 blocking 작업은 Tokio worker thread에서 그대로 수행됩니다.

**성능 영향**

cache file 수가 F라면 scan O(F), sort O(F log F), metadata syscall O(F)가 Tokio worker를 점유합니다. attachment download가 잦을 경우 매 download마다 cache 전체를 재스캔합니다.

**권장 수정**

- 최소 수정은 `tokio::task::spawn_blocking(move || enforce_cache_limit(...))`입니다.
- 더 좋은 구조는 cache index에 `(path, size, last_access)`를 유지해 매 download마다 전체 디렉터리를 스캔하지 않는 것입니다.
- eviction trigger를 매 파일 저장마다 실행하지 말고 high-water mark 또는 debounce/batch 방식으로 제한합니다.

---

### RUST-10 — Medium — relay Google ID token 검증마다 JWKS 네트워크 fetch가 발생합니다

**근거**

- `maho/relay/src/auth/google.rs:218-240`
  - `fetch_jwks()`는 매 호출 시 `worker::Fetch::Url(GOOGLE_JWKS_URL).send().await`를 수행하고 JSON을 parse합니다.
- `maho/relay/src/auth/google.rs:242-260`
  - `verify_id_token()`은 claim validation 뒤 항상 `fetch_jwks().await?`를 호출합니다.

**성능 영향**

Google login마다 외부 network RTT와 JSON parse가 추가됩니다. 인증 traffic이 증가하면 latency, Google endpoint dependency, Worker subrequest 비용이 함께 증가합니다.

**권장 수정**

- JWKS 응답의 `Cache-Control`/`max-age`를 존중한 in-memory 또는 Cloudflare Cache API/KV cache를 사용합니다.
- `kid` miss가 발생한 경우에만 강제 refresh하도록 합니다.
- cache에는 raw JWK뿐 아니라 RSA public key material을 predecode하여 modulus/exponent base64 decode와 `RsaPublicKey::new` 반복 비용도 줄일 수 있습니다.

---

### RUST-11 — Low — semantic search마다 query embedding과 result ID를 복사합니다

**근거**

- `maho/crates/maho-core/src/memory_manager.rs:148-157`
  - `Embedding(query_embedding.to_vec())`로 매 검색마다 query vector 전체를 새 heap allocation/복사합니다.
  - 결과에서 `item.value.clone()`으로 ID String을 다시 복사합니다.

**성능 영향**

단일 검색에서는 작지만 semantic memory 검색이 chat turn마다 반복되고 embedding dimension이 크면 allocation pressure가 누적됩니다.

**권장 수정**

- instant-distance API가 borrowed query를 허용하는지 확인하고, 가능하면 `EmbeddingRef<'a>` 또는 reusable query buffer를 사용합니다.
- 결과 API가 소유 String을 반드시 요구하지 않는다면 `&str`/`Arc<str>` 또는 interned ID를 사용합니다.
- `Search` 객체도 session/manager 단위로 재사용할 수 있는지 검토합니다.

---

## 추가 관찰

### maho-types / maho-engine / maho-import / maho-agent / maho-browser-mcp / maho-cli / maho-jni

이번 정적 조사에서 위 crate들에서도 clone/serde/spawn 사용은 다수 존재했으나, 현재 Chromium desktop startup 또는 반복적인 backend hot path에 직접 연결된다는 근거가 충분한 항목만 본 보고서의 finding으로 승격했습니다. 특히 `maho-jni`의 JSON serialization은 Android JNI boundary 특성상 별도 mobile 성능 분석에서 우선 검토하는 편이 적절합니다. `maho-import`의 파일/crypto 비용은 import 작업이 명시적으로 실행될 때 발생하므로 desktop browser startup critical path와는 구분했습니다.

### Lock contention

확인된 코드에는 `Mutex`가 여러 곳 존재하지만, 정적 코드만으로 실제 contention이 높다고 단정할 근거는 제한적이었습니다. 다만 FFI callback registry의 `Mutex<Vec<...>>`는 RUST-06처럼 dispatch마다 lock 후 전체 vector clone을 수행하므로 read-mostly snapshot 구조로 바꾸는 것이 명확한 개선점입니다.

## 제안하는 구현 순서

1. **Startup phase 분리**: `CreateWithStorage`를 lightweight open으로 축소하고 bulk hydrate/HNSW/content blocker rebuild를 background phase로 이동합니다.
2. **Memory index persistence**: HNSW snapshot + versioning + incremental update를 도입합니다.
3. **Storage bulk APIs**: boost active-state join, startup manager별 bulk query, migration versioning을 구현합니다.
4. **Restore algorithm 개선**: split-view 복원용 HashMap index를 사전 생성합니다.
5. **FFI update ABI 개선**: borrowed/Arc CoreUpdate, registry snapshot, delta payload 또는 typed ABI를 적용합니다.
6. **Runtime/I/O 정리**: maho-core Tokio runtime을 통합하고 mail-core blocking I/O를 blocking executor로 이동합니다.
7. **Relay cache**: JWKS cache와 `kid`-miss refresh 정책을 도입합니다.

## 검증 범위 및 제약

- 본 작업은 **정적 read-only 분석**입니다.
- 요청에 따라 `cargo build`, `cargo test`, `cargo check`, benchmark는 실행하지 않았습니다.
- 실제 wall-clock 영향도는 별도 trace가 필요합니다. 후속 검증 시 Chromium startup trace, Rust `tracing` span, SQLite `EXPLAIN QUERY PLAN`, memory fact 수별 HNSW rebuild benchmark를 권장합니다.
- 본 작업에서 수정 대상은 이 보고서 파일 `docs/performance/perf-report-rust-backend.md` 하나뿐이며, Rust/C++ 소스 파일은 수정하지 않습니다.
