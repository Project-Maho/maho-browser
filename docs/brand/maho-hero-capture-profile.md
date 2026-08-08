# Maho HyperFrames 랜딩 히어로 캡처 프로필 런북

## 목적과 범위 밖 항목

이 문서는 Maho HyperFrames 랜딩 히어로에 넣을 제품 장면을 일관되게 준비하고
캡처하는 절차입니다. Spaces, Split View, Command Bar, Maho AI 패널 장면은 모두
실제 Maho Browser에서 캡처합니다. 브라우저 창, 설정 화면, 탭, 패널을 HTML이나
정적 목업으로 다시 만들면 안 됩니다.

이 문서의 범위는 캡처 준비, 장면 상태, 검수, 산출물 이름까지입니다. 브라우저
기능 변경, 랜딩 구현, 새 프로필 생성, 스크린샷 생성은 이 문서의 작업 범위가
아닙니다.

## 공통 캡처 조건

- 일반 탐색용 프로필과 분리된 캡처 전용 `user-data-dir`만 사용합니다. 아래 경로는 개인 탐색, 로그인, 동기화에 사용하면 안 됩니다.
- 모든 프로필은 개인 데이터, 개인 계정, 방문 기록, 자동 완성, 저장된 비밀번호, 동기화된 탭이 없는 상태여야 합니다.
- 브라우저 창은 `1440 x 1000`, DPR 1로 고정하고, 페이지 확대율은 100%로 맞춥니다.
- 시스템 알림, 브라우저 알림, 다운로드 안내, 권한 요청, 확장 프로그램 배지, 운영체제 메뉴 막대 팝업이 보이면 안 됩니다.
- 사이트의 쿠키 배너, 로그인 유도창, 구독 유도창, 자동 재생 오버레이를 닫은 뒤
  캡처합니다. 닫을 수 없다면 해당 페이지를 다른 공개 페이지로 바꾸고 장면
  목적은 유지합니다.
- 화면 가장자리, 주소 표시줄, 탭 행, 사이드바, 분할선, 패널은 실제 브라우저
  렌더링을 그대로 남깁니다. 후반 작업으로 UI 구조를 추가하거나 바꾸면 안 됩니다.

## 캡처 전용 프로필

각 장면은 아래의 정확한 격리 경로로 실행합니다. 같은 장면을 다시 캡처할 때에도 해당 장면의 경로만 사용합니다.

| 장면 | `user-data-dir` |
| --- | --- |
| Spaces 개요 | `.omo/capture-profiles/maho-hero/spaces` |
| Split View | `.omo/capture-profiles/maho-hero/split-view` |
| Command Bar | `.omo/capture-profiles/maho-hero/command-bar` |
| Maho AI 패널 | `.omo/capture-profiles/maho-hero/agent-panel` |

실행 시에는 Maho Browser 바이너리에 장면별 경로 하나만 전달합니다. 예를 들어 Spaces 장면은 다음 형태를 사용합니다.

```sh
maho-browser-binary --user-data-dir="$PWD/.omo/capture-profiles/maho-hero/spaces"
```

## Spaces 장면 구성

Spaces 개요는 `spaces` 프로필에서만 준비합니다. 아래 네 Space는 이름, 색상, 타일,
탭 구성을 그대로 맞춥니다. Launch Space의 색상은 보라색이 아니라 오렌지 핑크이며,
권장값은 `#FF7A6B`입니다.

### Launch

- 색상: 오렌지 핑크 `#FF7A6B`
- 즐겨찾기 타일: Maho Home, Maho About, Maho Download
- 고정 탭: Maho Home, Maho About
- 일반 탭: Maho Why, Maho Privacy
- 활성 즐겨찾기 탭: Maho Home
- 로드된 페이지: `https://mahobrowser.com/`

### Media

- 색상: 핑크 `#FF6F91`
- 즐겨찾기 타일: YouTube, Google, Vimeo
- 고정 탭: YouTube, Vimeo
- 일반 탭: Google, Music for Programming
- 활성 즐겨찾기 탭: YouTube
- 로드된 페이지: `https://www.youtube.com/`

### Research

- 색상: 그린 `#7FA879`
- 즐겨찾기 타일: Wikipedia, MDN Web Docs, NASA
- 고정 탭: Wikipedia, MDN Web Docs
- 일반 탭: NASA, Google Scholar
- 활성 즐겨찾기 탭: Wikipedia
- 로드된 페이지: `https://www.wikipedia.org/`

### Reading

- 색상: 골드 `#D9A75B`
- 즐겨찾기 타일: Aeon, Longreads, The New Yorker
- 고정 탭: Aeon, Longreads
- 일반 탭: The Guardian Long Read, NPR
- 활성 즐겨찾기 탭: Aeon
- 로드된 페이지: `https://aeon.co/`

1. 네 Space를 위 순서대로 만들고, 색상은 각 Space의 Hex 값을 적용합니다.
2. 각 Space에서 즐겨찾기 타일, 고정 탭, 일반 탭을 위 목록의 항목과 순서대로 준비합니다.
3. 캡처 직전에는 Launch Space를 선택하고, `Maho Home` 즐겨찾기 탭이 활성 상태이며
   `https://mahobrowser.com/`이 완전히 로드됐는지 확인합니다.
4. Space 선택 표시, 타일, 탭 상태가 한 화면에서 읽히는 실제 브라우저 상태를 캡처합니다.

### Spaces 합격 기준

- 네 Space가 모두 보이고 이름과 색상은 위 구성과 일치합니다.
- Launch 색상은 `#FF7A6B` 계열의 오렌지 핑크이며, 보라색으로 보이지 않습니다.
- Launch가 선택돼 있고 `Maho Home` 즐겨찾기 탭이 활성 상태입니다.
- 개인 탭, 개인 계정 이름, 빈 기본 Space, 알림, 팝업이 없습니다.

### Spaces 반려 기준

- Launch가 보라색이거나 다른 색상으로 설정됐습니다.
- 타일, 고정 탭, 일반 탭 중 하나가 누락됐거나 위 구성과 다른 Space에 있습니다.
- 즐겨찾기 탭이 아닌 일반 탭이 활성 상태입니다.
- HTML이나 이미지 편집으로 만든 사이드바, 탭, Space UI가 포함됐습니다.

## Split View 장면 구성

Split View는 `split-view` 프로필에서 Media Space로 준비합니다. 이 장면은 두 상태를
남깁니다. 첫 번째는 드래그 중인 네이티브 분할 미리보기이고, 두 번째는 드롭 후의
정착 상태입니다.

1. Media Space를 선택하고 YouTube를 활성 대상 탭으로 엽니다.
2. Google을 일반 탭으로 열어 둡니다. Google은 고정 탭이나 즐겨찾기 탭이 아니어야 합니다.
3. Google 탭 행을 잡아 YouTube 탭의 아래쪽 3분의 2 영역 위로 드래그합니다.
4. Maho Browser가 그리는 네이티브 Split View 미리보기가 나타난 상태를 확인하고 드래그 중 장면을 캡처합니다.
5. 같은 위치에 드롭합니다.
6. 정착 상태에서 왼쪽은 YouTube, 오른쪽은 Google이고, 두 패널은 50:50 너비여야 합니다.
7. 드롭 후에는 왼쪽 YouTube 패널을 활성 상태로 만듭니다.

### Split View 합격 기준

- 드래그 장면에서 Google 탭 행이 YouTube의 아래쪽 3분의 2 위에 있고 네이티브 분할 미리보기가 보입니다.
- 정착 장면은 왼쪽 YouTube, 오른쪽 Google, 50:50 분할입니다.
- 정착 장면에서 활성 포커스는 YouTube 패널에 있습니다.
- Google은 드래그를 시작할 때 일반 탭이었습니다.

### Split View 반려 기준

- 수동으로 크기를 조정해 만든 분할이거나, 앱 외부에서 합성한 미리보기입니다.
- YouTube와 Google의 좌우 순서가 바뀌었거나 분할 비율이 50:50이 아닙니다.
- Google이 고정 탭 또는 즐겨찾기 탭으로 준비됐습니다.
- 드롭 뒤 Google 패널이 활성 상태입니다.

## Command Bar 장면 구성

Command Bar는 `command-bar` 프로필에서 준비합니다. 설정 화면과 명령 검색 결과는 한 번의 실제 브라우저 상태로 캡처합니다.

1. 실제 Maho Browser의 Content Blocker Settings 페이지를 엽니다.
2. Command Bar를 Search 모드로 엽니다.
3. 검색어로 정확히 `content blocker`를 입력합니다.
4. 실제 검색 결과의 첫 번째 `Content Blocker` 항목을 선택 상태로 둡니다.
5. 선택한 항목은 활성화하지 않습니다. Enter를 누르거나 설정 화면을 전환하면 안 됩니다.

### Command Bar 합격 기준

- 배경은 실제 Content Blocker Settings 페이지입니다.
- Command Bar가 Search 모드이고 입력값은 `content blocker`입니다.
- 첫 번째 실제 `Content Blocker` 결과가 선택돼 있으며, 선택만 된 상태입니다.
- 설정 페이지 이동, 결과 실행, 토글 변경이 없습니다.

### Command Bar 반려 기준

- 일반 웹 검색 결과, 가짜 설정 화면, HTML로 만든 명령 팔레트가 보입니다.
- Search 모드가 아니거나 검색어 철자가 다릅니다.
- 첫 번째 결과가 선택되지 않았거나 항목을 활성화했습니다.

## Maho AI 패널 장면 구성

Maho AI 패널은 `agent-panel` 프로필에서 준비합니다. 실제 공개 Maho 페이지
`https://mahobrowser.com/`를 열고, 오른쪽에 도킹된 실제 Maho AI 패널을 사용합니다.

1. 새롭고 깨끗한 세션을 시작합니다. 이전 대화, 응답, 첨부, 작업 기록은 남아 있으면 안 됩니다.
2. 공개 Maho 페이지가 완전히 로드된 상태에서 실제 Maho AI 패널을 오른쪽 도킹 위치로 엽니다.
3. 패널에서 `Current Page`가 첨부된 상태를 확인합니다.
4. 입력창에 `What are the key ideas on this page?`를 입력합니다.
5. 프롬프트는 보내지 않습니다. 응답, 실행 상태, 자동 행동을 암시하는 UI를 만들거나 추가하면 안 됩니다.

### Maho AI 패널 합격 기준

- 실제 공개 Maho 페이지와 실제 오른쪽 도킹 Maho AI 패널이 함께 보입니다.
- 세션이 깨끗하고 이전 메시지나 응답이 없습니다.
- `Current Page`가 첨부돼 있습니다.
- 입력창에는 정확히 `What are the key ideas on this page?`가 있으나 전송되지 않았습니다.

### Maho AI 패널 반려 기준

- 패널이 떠 있거나 왼쪽에 도킹돼 있거나, HTML로 재현됐습니다.
- 개인 페이지, 로그인된 계정, 이전 세션 내용이 보입니다.
- `Current Page`가 빠졌거나 프롬프트가 전송됐습니다.
- 실제 제품에 없는 응답, 자동 실행, 자율 행동을 암시하는 요소를 추가했습니다.

## 최종 캡처 체크리스트

- [ ] 해당 장면의 정확한 `user-data-dir`에서 실행했습니다.
- [ ] 개인 데이터와 알림을 제거했고, 1440 x 1000, DPR 1, 100% 확대율을 확인했습니다.
- [ ] 장면의 실제 Maho Browser UI만 보이며 HTML 재현 또는 합성 UI가 없습니다.
- [ ] Spaces 장면은 Launch의 오렌지 핑크 `#FF7A6B`를 포함한 네 Space 구성을 충족합니다.
- [ ] Split View 장면은 네이티브 드래그 미리보기와 정착 상태를 각각 확인했습니다.
- [ ] Command Bar 장면은 결과를 선택만 하고 활성화하지 않았습니다.
- [ ] Maho AI 장면은 `Current Page` 첨부와 미전송 프롬프트만 포함합니다.
- [ ] 모든 장면을 100% 크기로 검토해 잘린 UI, 팝업, 로딩 표시, 개인 정보가 없는지 확인했습니다.

## 캡처 산출물 경로

캡처 파일은 모두 아래 디렉터리에 둡니다. 이 런북 자체는 캡처 파일이나 프로필을 만들지 않습니다.

### Spaces 개요

```text
.omo/capture-profiles/maho-hero/evidence/spaces-overview.png
```

### Split View 드래그 미리보기

```text
.omo/capture-profiles/maho-hero/evidence/split-view-drag-preview.png
```

### Split View 정착 상태

```text
.omo/capture-profiles/maho-hero/evidence/split-view-settled.png
```

### Command Bar

```text
.omo/capture-profiles/maho-hero/evidence/command-bar-content-blocker.png
```

### Maho AI 패널

```text
.omo/capture-profiles/maho-hero/evidence/agent-panel-current-page.png
```

산출물을 제출하기 전에는 위 장면별 합격 기준과 반려 기준을 다시 대조합니다.
하나라도 반려 기준에 해당하면 해당 프로필에서 장면을 다시 준비해 캡처합니다.
