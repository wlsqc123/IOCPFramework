#pragma once

#include <WinSock2.h>
#include <Windows.h>
#include <optional>
#include "IOCPOverlapped.h"
#include "../../common/Types.h"

namespace IOCPFramework {

// GetQueuedCompletionStatus 결과
struct CompletionResult {
    u64             completionKey;
    IOCPOverlapped* pOverlapped;
    u32             bytesTransferred;
    bool            success;
};

// IOCP CompletionPort RAII 래퍼
class IOCPCore {
public:
    IOCPCore();
    ~IOCPCore();

    IOCPCore(const IOCPCore&) = delete;
    IOCPCore& operator=(const IOCPCore&) = delete;
    IOCPCore(IOCPCore&& other) noexcept;
    IOCPCore& operator=(IOCPCore&& other) noexcept;

    // IOCP 포트 생성 (concurrentThreads: 0 = CPU 코어 수)
    bool Init(u32 concurrentThreads = 0);

    // 소켓/핸들을 IOCP에 등록
    bool Register(HANDLE handle, u64 completionKey);

    // 완료 통지 대기 (타임아웃 시 nullopt)
    std::optional<CompletionResult> Dispatch(u32 timeoutMs = INFINITE);

    // 사용자 정의 완료 패킷 전송 (예: 종료 신호)
    bool PostCompletion(u64 completionKey, IOCPOverlapped* pOverlapped = nullptr);

    HANDLE GetHandle() const { return m_hIOCP; }
    bool   IsValid()  const { return m_hIOCP != INVALID_HANDLE_VALUE && m_hIOCP != nullptr; }

private:
    HANDLE m_hIOCP;

    void Close();
};

} // namespace IOCPFramework
