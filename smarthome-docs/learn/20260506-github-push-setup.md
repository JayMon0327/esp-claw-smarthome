# 2026-05-06 — esp-claw-smarthome GitHub 저장소 셋업 및 첫 푸시

## 작업 개요
사전 조사 + 플랜 + Day 0 발견 결과를 보존하기 위해, 로컬에 클론한 espressif/esp-claw 저장소를 사용자 GitHub의 빈 저장소(`JayMon0327/esp-claw-smarthome`)로 푸시. espressif 커밋 히스토리를 보존하면서 사용자 작성 문서를 별도 폴더로 추가.

## 결과 요약
- **GitHub 저장소**: https://github.com/JayMon0327/esp-claw-smarthome (`main` 브랜치)
- **새 커밋**: `cc9ee24` — `smarthome-docs/`에 4개 파일 추가 (총 1809줄)
- **espressif 커밋 히스토리 100% 보존** (포크 스타일 셋업)
- **디렉토리 평탄화 완료**: 작업 루트 = `~/Desktop/esp-claw/esp-claw/`

## 주요 의사결정

### 1. 포크 스타일 vs 평탄화 — 포크 스타일 채택
3가지 옵션을 비교:
- (a) 평탄화: `git init` 후 espressif `.git` 삭제 → 히스토리 손실
- (b) 포크 스타일: 기존 espressif 저장소를 그대로 사용, origin만 교체 → 히스토리 보존 ✅
- (c) 서브모듈: 바깥 새 repo + esp-claw를 submodule로 → git-correct하지만 복잡

추후 esp-claw 코드를 수정해 스마트홈 cap을 추가할 계획 + upstream 변경사항 pull 가능성 → **(b) 채택.**

### 2. `docs/` 충돌 회피 — `smarthome-docs/`로 명명
espressif 저장소는 이미 `docs/`(Astro 기반 공식 문서 사이트)를 보유. 사용자 docs를 같은 이름으로 두면 충돌. 저장소명(`esp-claw-smarthome`)과 일관되도록 `smarthome-docs/`로 명명.

### 3. 원격 설정 — origin/upstream 분리
- `origin` → `JayMon0327/esp-claw-smarthome` (사용자 작업용)
- `upstream` → `espressif/esp-claw` (업스트림 동기화용)
- 로컬 `master` → 원격 `main` 추적 (GitHub 기본 브랜치 컨벤션 따름)

## 핵심 학습

### 1. nested `.git` 디렉토리는 `git add` 시 gitlink로 처리됨
바깥 폴더에서 `git init && git add .`을 하면 안쪽 `.git` 보유 폴더는 submodule reference(SHA만 기록)로 잡혀 파일이 업로드되지 않음. 평탄화 푸시를 원하면 안쪽 `.git`을 먼저 제거해야 함.

### 2. 빈 GitHub 저장소의 기본 브랜치는 `main`
로컬이 `master`인 경우 `git push -u origin master:main`으로 매핑 푸시하면 추적 자동 설정.

### 3. 디렉토리 평탄화 시 hidden file 처리
`mv esp-claw/* .`은 hidden 파일 누락. `find esp-claw -mindepth 1 -maxdepth 1 -exec mv {} . \;`로 visible+hidden 모두 안전하게 이동.

### 4. `gh api repos/{owner}/{repo}/contents/{path}`로 푸시 직후 검증 가능
GitHub `size` 메타데이터는 푸시 직후 stale일 수 있으나 `contents/` API는 실시간 반영됨.

## 후속 액션
- Day 1 진행 시: `git checkout -b day1-bringup`로 작업 브랜치 분리 권장
- upstream 변경사항 동기화: `git fetch upstream && git merge upstream/master`
- 새 cap 추가 시: feature 브랜치 → PR 자체 리뷰 → main merge 흐름 권장

## 참고
- 푸시된 저장소: https://github.com/JayMon0327/esp-claw-smarthome
- 작업 루트: `/Users/wm-mac-01/Desktop/esp-claw/esp-claw/`
- 관련 사전 작업 로그: `smarthome-docs/learn/20260506-plan-cross-verify.md`
