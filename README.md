# IOCPFramework

Windows IOCP 기반 고성능 MMORPG 게임 서버 (C++20)

## 빌드 방법

```bash
mkdir build && cd build
cmake ..
cmake --build . --config Debug
```

## 실행

```bash
./build/bin/Debug/IOCPServer.exe
```

## 프로젝트 구조

```
IOCPFramework/
├── common/          # 공용 타입 정의
├── engine/          # IOCP 네트워크 엔진
│   ├── iocp/       # IOCP Core
│   ├── session/    # 세션 관리
│   ├── buffer/     # 버퍼 시스템
│   └── packet/     # 패킷 처리
└── bot/            # 스트레스 테스트 봇
```

## 기술 스택

- C++20
- Windows IOCP
- CMake
