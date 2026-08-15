# Windows 코드 서명 인증서 — 한국 사업자용 최저가·최간편 가이드

> 대상: Maho(비공개/상용 소프트웨어)를 배포하는 **한국 사업자**
> 목적: Windows `chrome.exe` / `MahoSetup.exe`에 붙일 **실제로 신뢰되는(SmartScreen "알 수 없는 게시자" 제거)** 코드 서명 인증서를, 가장 싸고 쉽게 받는 법.
> 최종 조사: 2026-08 기준.

---

## 결론 (TL;DR)

1. **무료로 신뢰되는 인증서는 사실상 없음.** 무료(SignPath Foundation, Certum Open Source)는 전부 **오픈소스 전용**이라 비공개 상용인 Maho에는 부적격.
2. **Azure Trusted Signing(월 ~$10, 가장 쌌던 정식 경로)은 한국에서 발급 불가.** 현재 미국·캐나다·EU·영국 법인 + 미국·캐나다 개인만 신원검증 가능. 한국 법인은 국가 목록에 없음(로드맵 미정).
3. 따라서 한국 사업자에게 **가장 싸고 쉬운 실전 경로 = 해외 CA의 OV 코드서명 인증서 + 클라우드 HSM(토큰 없음)**.
   - **추천 1순위: Certum "Standard Code Signing in the Cloud" (SimplySign), 연 ~$116** — 평판 좋은 EU CA(Asseco Certum), 클라우드 서명(물리 토큰 불필요), 자동화/CI 가능.
   - **최저가: Nicsrs Cloud Code Signing, 연 ~$75** — 더 저렴, 클라우드 HSM(FIPS 140-3). 소규모 리셀러라 평판은 Certum보다 덜 알려짐.
   - **경고 즉시 제거가 필요하면: EV 클라우드**(Certum EV ~$226/년) — SmartScreen 평판 **즉시** 통과. OV는 다운로드 누적으로 평판이 서서히 쌓임.
4. **국내 CA(한국전자인증/한국정보인증/코리아SSL 등)는 비추천**: 2023-05 이후 OV 코드서명이 **USB 하드웨어 토큰으로만** 발급됨 → 배송·수령 필요, 자동/원격 서명이 번거롭고, 가격도 대체로 더 비쌈.

---

## 왜 Azure Trusted Signing은 안 되나 (한국)

Microsoft Trusted Signing(현 "Azure Artifact Signing")은 인증서 수명관리까지 대신해 주는 월 ~$10짜리 최저가 정식 서비스지만, **신원검증(Identity Validation) 국가에 한국이 없음**:

- 공식: "Trusted Signing is currently available to organizations in the USA, Canada, the European Union, and the United Kingdom, as well as individual developers in the USA and Canada." ([Azure/artifact-signing-action#81](https://github.com/Azure/artifact-signing-action/issues/81))
- 한국 법인 지원 문의에 대해 MS Learn Q&A에서 "미지원, 로드맵 미정" 취지 답변 ([learn.microsoft.com Q&A](https://learn.microsoft.com/ko-kr/answers/questions/5930526/is-azure-trusted-signing-artifact-signing-availabl), [Q&A2](https://learn.microsoft.com/en-in/answers/questions/5923490/does-azure-artifact-signing-public-trust-support-s))
- 검증 폼에서 국가 드롭다운에 US/Canada만 노출 ([StackOverflow](https://stackoverflow.com/questions/79553281/azure-trusted-signing-identity-validation-only-in-canada-or-united-states))

→ **한국 사업자는 현재 Azure 경로 제외.** (추후 APAC 확대되면 최우선 재검토 대상.)

---

## 실전 옵션 비교 (OV, 클라우드, 토큰 불필요)

> 2023년 CA/B 포럼 규정으로 **모든 OV/EV 코드서명 개인키는 FIPS 하드웨어 토큰 또는 클라우드 HSM에 보관 필수**. 클라우드(HSM) 방식을 골라야 물리 토큰 배송·연결 없이 `signtool`/CI로 서명 가능.

| 제공처 | 종류 | 연 가격(대략) | 토큰 | 비고 |
|---|---|---|---|---|
| **Nicsrs Cloud Code Signing** | OV | **~$75** | 클라우드 HSM | 최저가, FIPS 140-3, CI 연동 ([nicsrs.com](https://www.nicsrs.com/cloud-code-signing)) |
| **Certum Standard Code Signing in Cloud (SimplySign)** | OV | **~$116** | 클라우드(SimplySign) | 평판 좋은 EU CA, "Unknown publisher" 제거, SmartScreen 평판 축적 ([shop.certum.eu](https://shop.certum.eu/standard-code-signing-in-the-cloud.html), [SSLmentor $116](https://www.sslmentor.com/certum/certumcodecloud)) |
| SSL Dragon (Comodo/Sectigo/DigiCert) | OV | $219~ | 클라우드 HSM 옵션 | 1~7영업일 발급 ([ssldragon.com](https://www.ssldragon.com/ssl-certificates/code-signing/)) |
| SignMyCode | OV | ~$216 | 클라우드 HSM | ([signmycode.com](https://signmycode.com/ov-code-signing)) |
| SSL.com eSigner | OV | 상위가 | 클라우드(eSigner) | CI/CD 서명 서비스 ([ssl.com](https://www.ssl.com/products/software-integrity/signing-service/)) |
| **Certum EV Code Signing in Cloud** | **EV** | **~$226** | 클라우드(SimplySign) | **SmartScreen 즉시 통과**, 검증 더 엄격 ([SSLmentor EV](https://www.sslmentor.com/certum/certumcodecloudev)) |
| GoGetSSL EV Cloud | EV | ~$545 | 클라우드 | ([gogetssl.com](https://www.gogetssl.com/sslcerts/ev-cloud-codesigning-ssl/)) |

### OV vs EV — 무슨 차이?
- **OV**: "알 수 없는 게시자" 이름 경고는 사라지고 **검증된 회사명**이 표시됨. 단, SmartScreen "Windows가 PC를 보호했습니다" 경고는 **다운로드가 쌓여 평판이 생길 때까지** 일부 사용자에게 뜰 수 있음.
- **EV**: 서명 즉시 SmartScreen 평판 부여 → **처음부터 경고 없음**. 대신 가격·검증(사업자 실체·권한 확인)이 더 빡셈.
- Maho처럼 다운로드가 아직 적은 신규 배포는, 예산 되면 **EV(Certum EV ~$226)**가 사용자 경험상 확실. 예산 최소면 **OV(Certum ~$116 / Nicsrs ~$75)**로 시작해 평판을 쌓는 것도 가능.

---

## 국내 CA는 왜 비추천인가
- 한국전자인증(CrossCert), 한국정보인증(KICA), 코리아SSL(한국디지털인증), SecureSign, Ucert 등은 Sectigo/DigiCert OV/EV를 재판매.
- **2023-05-08부터 국내 리셀러의 SECTIGO OV 코드서명은 보안 USB 하드웨어 토큰으로만 전달** ([KICA 공지](https://www.kicassl.com/cstmrsuprt/ntc/searchNtcDetail.sg?ntcSeq=1021)). → 배송·물리연결 필요, 원격/CI 자동서명 불리.
- 가격도 해외 클라우드 리셀러 대비 대체로 높음(비교: [sslcert.co.kr](https://www.sslcert.co.kr/products/CodeSigning), [ucert.co.kr](https://www.ucert.co.kr/codesign/allcodesign)).
- 장점은 한국어 지원·세금계산서 처리. 자동화보다 국내 지원이 중요하면 고려.
- 참고: **2026-03-01부터 코드서명 인증서 유효기간 단축 예정** — 다년 발급 시 재발급/토큰 이슈 유의 ([SecureSign 공지](https://www.sslcert.co.kr/products/CodeSigning)).

---

## 발급에 필요한 것 (한국 사업자 → 해외 CA OV 클라우드)
해외 CA도 **글로벌 사업자**를 검증하므로 한국 사업자 발급 가능(Azure와 달리 국가 제한 없음). 일반 준비물:
1. **사업자등록증** (영문 상호/주소 일치 필요할 수 있음).
2. **D-U-N-S 번호** (없으면 무료 발급 가능, 1~2주 소요) 또는 CA가 인정하는 제3자 사업자 조회.
3. **검증 전화** (등록된 대표번호로 CA가 확인 전화 — 전화번호부/제3자 DB 등재 필요할 수 있음).
4. 영문 회사명·주소·담당자 이메일.
- 발급 소요: OV 보통 1~7영업일(사업자 검증 상태에 따라).

---

## Maho 빌드 파이프라인 연동 (인증서 확보 후)
클라우드 서명이면 물리 토큰 없이 CI/원격에서 서명 가능:
1. Certum SimplySign / SSL.com eSigner / Nicsrs 클라우드 클라이언트 설치 → 로컬 인증서 저장소에 인증서 노출.
2. `signtool sign /fd sha256 /tr <RFC3161 TSA> /td sha256 /sha1 <thumbprint> MahoSetup.exe` (또는 CA 제공 클라우드 서명 CLI).
3. `build_maho.py`의 Windows 설치본 빌드 뒤 서명 스텝을 추가하거나 `release_local.py`에 `MAHO_WIN_CERT_SHA1`(thumbprint) 경로로 연결. (현재 코드에 `MAHO_WIN_CERT_PFX`/`_SHA1` 훅 자리 있음.)
4. `chrome.exe`, `chrome.dll`(선택), `MahoSetup.exe` 모두 서명 + 타임스탬프.

---

## 무료 경로가 왜 배제되는지 (기록)
- **SignPath Foundation**(무료, 신뢰됨): **오픈소스 전용** — 공개 리포에서 빌드된 바이너리만 서명, 게시자가 "SignPath Foundation"으로 표기 ([signpath.org](https://signpath.org/)). Maho는 비공개라 부적격.
- **Certum Open Source Code Signing**(~$49): 오픈소스 자격 필요 ([shop.certum.eu](https://shop.certum.eu/open-source-code-signing.html)).
- 이들을 쓰려면 Maho 소스 공개가 전제 → 현행 비공개 정책과 충돌(무단 사용 불가).
- **자체서명(self-signed)**: 무료지만 Windows가 신뢰 안 함 → SmartScreen/UAC 경고 그대로. 공개 배포엔 무의미.

---

## 추천 액션 (예산순)
- **최소 예산·간편:** Certum Standard Code Signing in Cloud (~$116/년, SimplySign 클라우드) — 무난한 1순위.
- **극최저가:** Nicsrs Cloud OV (~$75/년) — 리셀러 신뢰도만 확인.
- **사용자 경험 최상(경고 즉시 제거):** Certum **EV** Cloud (~$226/년).
- 인증서 확보되면 위 "파이프라인 연동" 절대로 `signtool` 서명 스텝을 붙이면 됨. 그 전까지는 **미서명 배포** 유지.
