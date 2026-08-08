# Windows 전용 컴파일 에러 수정: `SidebarSectionDropTarget` incomplete type

- **대상 파일**: `maho-chromium/browser/ui/views/sidebar/maho_sidebar_tab_list_view.cc` (수정), `.h` (수정 없음)
- **증상**: macOS 빌드는 정상, Windows(clang-cl) 빌드에서만 아래 에러 발생

```text
maho_sidebar_tab_list_view.cc(1235,28): error: variable has incomplete type 'SidebarSectionDropTarget'
raw_ptr.h(668,12): error: static_cast from 'views::View *' to 'maho::SidebarSectionDropTarget *',
    which are not related by inheritance, is not allowed
maho_sidebar_tab_list_view.h(239,16): note: forward declaration of 'maho::SidebarSectionDropTarget'
```

## 근본 원인

같은 이름의 **서로 다른 두 타입**이 만들어져 있었다.

1. `.cc`의 익명 네임스페이스(파일 상단 `namespace {`) 안에 실제 클래스가 정의되어 있었다.
   실제 정체는 `(anonymous namespace)::SidebarSectionDropTarget`.
2. `.h`의 `MahoSidebarTabListView`(`namespace maho`) 안에 있는
   `friend class SidebarSectionDropTarget;` 선언은 **별개의, 정의된 적 없는**
   `maho::SidebarSectionDropTarget` 타입을 선언한다.

### 왜 Windows에서만 실패하는가

표준 C++에서는 friend 선언만으로 도입된 이름은 일반 이름 탐색(ordinary lookup)에
보이지 않는다. 그래서 macOS clang에서는 `.cc`의 사용 지점에서 이름 탐색이
익명 네임스페이스의 **완전한** 클래스를 찾아 정상 컴파일된다.

반면 Windows 빌드는 clang-cl로 컴파일되며, clang-cl은 MSVC 호환 모드
(`-fms-compatibility`)가 기본이다. 이 모드에서는 MSVC 버그 호환을 위해
**friend 선언이 이름 탐색에 보인다.** 따라서 `namespace maho` 안의 사용 지점에서
이름 탐색이 상위 스코프(익명 네임스페이스)까지 내려가기 전에
`maho::SidebarSectionDropTarget`(불완전 타입)을 먼저 찾아버린다.

그 결과:

- `SidebarSectionDropTarget target(...)` 지역 변수 생성 → **불완전 타입 인스턴스화 불가** (1235, 1244행)
- `static_cast<SidebarSectionDropTarget*>(pinned_section_target_)` — 멤버가
  `base::raw_ptr<views::View>`이므로 `raw_ptr`의 템플릿 변환 연산자
  (`raw_ptr.h:668`)가 `maho::SidebarSectionDropTarget*`로 인스턴스화되는데,
  불완전 타입은 `views::View`와의 상속 관계를 증명할 수 없어 **다운캐스트 불가**
  (4228, 4295, 4453, 4475행)

즉, 에러 메시지의 `raw_ptr`은 결과일 뿐이고, 원인은 **익명 네임스페이스 정의와
friend 선언의 타입 정체성 불일치**다.

## 로컬 재현 (진단 검증)

macOS의 일반 clang으로도 `-fms-compatibility` 플래그를 켜면 동일한 에러가 재현된다.
핵심은 실제 컴파일 순서(헤더의 friend 선언이 익명 네임스페이스 정의보다 먼저)를
그대로 따르는 것이다:

```cpp
namespace views { struct View { virtual ~View() = default; }; }

// ---- 헤더에 해당 ----
namespace maho {
class MahoSidebarTabListView {
  friend class SidebarSectionDropTarget;  // maho::SidebarSectionDropTarget 선언
 public:
  views::View* pinned_section_target_ = nullptr;
};
}  // namespace maho

// ---- .cc에 해당 ----
namespace {
class SidebarSectionDropTarget : public views::View { /* ... */ };
}  // namespace

namespace maho {
void f(MahoSidebarTabListView* v) {
  auto* t = static_cast<SidebarSectionDropTarget*>(v->pinned_section_target_);
}
}  // namespace maho
```

```console
$ clang++ -std=c++20 -fsyntax-only repro.cc                      # macOS 모드 → 통과
$ clang++ -std=c++20 -fms-compatibility -fsyntax-only repro.cc   # clang-cl 모드
repro.cc: error: static_cast from 'views::View *' to 'SidebarSectionDropTarget *',
    which are not related by inheritance, is not allowed
note: 'SidebarSectionDropTarget' is incomplete   ← 실제 에러와 동일
```

## 선택한 수정: 클래스 정의를 `namespace maho`로 이동 (방향 1)

`.cc`에서 `SidebarSectionDropTarget` 클래스 정의(약 577행)를 감싸던 익명
네임스페이스를 그 지점에서 닫고, `namespace maho` 블록 안에 정의한 뒤 다시
익명 네임스페이스를 연다. **`.h`는 수정하지 않는다** — 기존
`friend class SidebarSectionDropTarget;`이 선언하는 `maho::SidebarSectionDropTarget`이
이제 `.cc`의 정의와 **동일한 타입**이 되기 때문이다.

```cpp
// maho_sidebar_tab_list_view.cc (수정 후 구조)

}  // namespace  ← SidebarSeparatorRow까지 정의하고 익명 네임스페이스를 닫음

namespace maho {

// Defined in namespace maho (not the anonymous namespace) so it is the same
// type as the `friend class SidebarSectionDropTarget;` declaration in
// MahoSidebarTabListView. Under clang-cl's MSVC compatibility mode that friend
// declaration is visible to ordinary name lookup, so an anonymous-namespace
// definition would leave the maho:: name incomplete at the static_cast sites
// below and fail to compile on Windows.
class SidebarSectionDropTarget : public views::View {
  // ... 본문 변경 없음 ...
};

}  // namespace maho

namespace {  ← 익명 네임스페이스 재개 (IsSidebarEligibleUrl부터)
```

### 왜 이 수정이 양 플랫폼 모두에서 올바른가

- **타입이 하나로 통일됨**: friend 선언(`maho::`), 정의(`maho::`), 모든 사용 지점이
  같은 완전한 타입을 가리킨다. `raw_ptr`의 변환 연산자도 완전한
  `views::View` 파생 타입으로 인스턴스화되므로 다운캐스트가 성립한다.
- **macOS(표준 clang)**: 사용 지점(`namespace maho` 내부)에서 이름 탐색이 정의를
  직접 찾는다. 익명 네임스페이스 블록의 사용처(`AddSectionContainer`의
  `std::make_unique<SidebarSectionDropTarget>`)는 기존 `using namespace maho;`
  (143행)를 통해 같은 타입으로 해석된다.
- **Windows(clang-cl, ms-compatibility)**: friend 선언으로 보이는 이름과 정의가
  같은 엔티티이므로 더 이상 불완전 타입이 아니다.
- **본문 이름 해석 유지**: 클래스 본문이 참조하는 헬퍼
  (`GetMahoDragFormatType`, `ReadMahoDragData`, `PlanSectionDrop`,
  `ExecuteDropPlan`, `ForwardDragToAutoScroller` 등)는 전부 `namespace maho`
  소속이라 그대로 해석되고, 익명 네임스페이스의 상수
  (`kSectionInsets`, `kRowSpacingDp`, `kRowCornerRadiusDp`,
  `kDropIndicatorOutlineThicknessDp`)는 익명 네임스페이스 멤버가 번역 단위
  전체에서 보이므로 계속 접근 가능하다.
- **부수 효과 없음**: 클래스는 `.cc` 안에서만 정의되므로 링커 심볼이 외부로
  노출되는 것 외에 동작 변화가 없고, 이 이름을 쓰는 다른 번역 단위는 없다
  (`maho_sidebar_interactive_test_base.cc`는 `GetClassName()` 문자열 비교만 함).

### 검토했던 대안과 기각 사유

| 대안 | 기각 사유 |
|---|---|
| (2) friend 선언 제거 후 접근 재설계 | 클래스가 `MahoSidebarTabListView`의 private 멤버에 접근하므로 friend가 필요하고, 익명 네임스페이스 타입에 표준적으로 friend를 걸려면 헤더에서 그 타입을 지칭할 방법이 없어 구조 변경이 커짐 |
| (3) `.get()` 경유 캐스트 | 이름이 여전히 불완전한 `maho::` 타입으로 해석되므로 단독으로는 해결 불가 — 결국 (1)이 필요 |
| `#if BUILDFLAG(IS_WIN)` 분기 | 타입 정체성 버그를 숨길 뿐이며 MUST NOT 항목 |

## 변경 요약

- `maho_sidebar_tab_list_view.cc`: 익명 네임스페이스를 클래스 정의 직전에 닫고,
  `SidebarSectionDropTarget` 정의를 `namespace maho { ... }`로 감싼 뒤 익명
  네임스페이스를 재개. 클래스 본문·사용처 코드는 변경 없음. 이유를 설명하는
  주석 추가.
- `maho_sidebar_tab_list_view.h`: 변경 없음.

## 검증 결과

1. **macOS 실제 빌드**: 해당 오브젝트만 타겟 컴파일 → 성공
   ```console
   $ cd chromium/src
   $ third_party/ninja/ninja -C out/Default \
       obj/maho/browser/ui/views/sidebar/maho_sidebar_core/maho_sidebar_tab_list_view.o
   [1/1] CXX obj/.../maho_sidebar_tab_list_view.o
   ```
2. **Windows 동작 검증(로컬 재현)**: 위 최소 재현 코드를
   `-fms-compatibility`로 컴파일 — 수정 전 구조는 실제와 동일한 에러로 실패,
   수정 후 구조(클래스를 `maho`에 정의)는 통과. 플래그 없는 표준 모드에서도
   양쪽 구조 모두 통과 → macOS 무회귀 확인.
3. **참조 지점 전수 확인**: 정의(587행), `std::make_unique<...>`(813행),
   지역 변수 생성(1249, 1258행), `static_cast<...>`(4242, 4309, 4467, 4489행),
   `.h`의 friend 선언(239행)이 모두 동일한 완전 타입
   `maho::SidebarSectionDropTarget`으로 해석됨.

## 교훈 (같은 유형의 잠재 버그 예방)

- 헤더의 `friend class X;`가 `.cc` 익명 네임스페이스의 클래스를 가리키게 하는
  패턴은 표준적으로도 서로 다른 타입을 만드는 함정이며, clang-cl의
  ms-compatibility 아래에서는 컴파일 에러로 표면화된다.
- Mac CI만 도는 오버레이 코드에서 `friend` + 익명 네임스페이스 + `raw_ptr`
  다운캐스트 조합이 보이면 동일 이슈를 의심할 것. 로컬에서
  `clang++ -fms-compatibility -fsyntax-only`로 저비용 선별 검사가 가능하다.
