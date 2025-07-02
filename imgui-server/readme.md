# ImGui DirectX11 게임서버 디버거

**Dear ImGui** + **DirectX11**을 사용한 게임서버 디버깅 도구 예시 프로젝트입니다.

## 🎮 개요

실제 게임서버에서 활용할 수 있는 디버깅 인터페이스의 예시를 보여줍니다:
- 실시간 서버 상태 모니터링 UI
- 플레이어/몬스터 데이터 시각화
- 관리자 제어 패널

현재는 시뮬레이션 데이터로 동작하며, 실제 게임서버와 연동하면 유용한 디버깅 도구가 될 수 있습니다.

## 🛠️ 기술 스택

- **UI**: Dear ImGui
- **렌더링**: DirectX 11 + Win32
- **패키지 관리**: vcpkg
- **언어**: C++

## 🚀 빌드 방법

### 1. vcpkg 설치

관리자 권한 명령 프롬프트:

```bash
git clone https://github.com/Microsoft/vcpkg.git C:\vcpkg
cd C:\vcpkg
.\bootstrap-vcpkg.bat
.\vcpkg integrate install
```

### 2. 라이브러리 설치

```bash
.\vcpkg install imgui[core,dx11-binding,win32-binding]:x64-windows
```

## ⚡ 실행 결과

- 서버 상태 대시보드 창
- 플레이어 목록 관리 창  
- 실시간 데이터 업데이트 시뮬레이션

## 💡 활용 방안

이 예시를 참고하여 **GUI가 포함된 게임서버**를 개발할 수 있습니다:

- 서버 상태를 실시간으로 시각화
- 복잡한 로그 대신 직관적인 GUI로 문제 파악
- 게임 내 오브젝트들을 맵에서 한눈에 모니터링