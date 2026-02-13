#pragma once

#include <WinSock2.h>
#include "../../common/Types.h"

namespace IOCPFramework {

// IOCP 비동기 작업 유형
enum class IOOperation : u8 {
    RECV       = 1,
    SEND       = 2,
    ACCEPT     = 3,
    DISCONNECT = 4,
};

// WSAOVERLAPPED 확장 구조체
// WSAOVERLAPPED가 첫 번째 멤버여야 OVERLAPPED* <-> IOCPOverlapped* 캐스팅 안전
struct IOCPOverlapped {
    WSAOVERLAPPED overlapped;   // 반드시 첫 번째 멤버
    IOOperation   operation;
    SessionId     sessionId;    // 소유 세션 ID (0 = 미할당)
    void*         ownerPtr;     // 추가 컨텍스트

    IOCPOverlapped() { Reset(); }

    void Reset() {
        ZeroMemory(&overlapped, sizeof(WSAOVERLAPPED));
        operation = IOOperation::RECV;
        sessionId = 0;
        ownerPtr  = nullptr;
    }

    static IOCPOverlapped* FromOverlapped(OVERLAPPED* pOv) {
        return reinterpret_cast<IOCPOverlapped*>(pOv);
    }
};

} // namespace IOCPFramework
