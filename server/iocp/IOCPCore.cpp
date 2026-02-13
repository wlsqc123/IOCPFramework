#include "IOCPCore.h"
#include <cstdio>

namespace IOCPFramework {

IOCPCore::IOCPCore()
    : m_hIOCP(INVALID_HANDLE_VALUE)
{
}

IOCPCore::~IOCPCore() {
    Close();
}

IOCPCore::IOCPCore(IOCPCore&& other) noexcept
    : m_hIOCP(other.m_hIOCP)
{
    other.m_hIOCP = INVALID_HANDLE_VALUE;
}

IOCPCore& IOCPCore::operator=(IOCPCore&& other) noexcept {
    if (this != &other) {
        Close();
        m_hIOCP = other.m_hIOCP;
        other.m_hIOCP = INVALID_HANDLE_VALUE;
    }
    return *this;
}

bool IOCPCore::Init(u32 concurrentThreads) {
    if (IsValid()) {
        printf("[IOCPCore] Already initialized\n");
        return false;
    }

    m_hIOCP = CreateIoCompletionPort(
        INVALID_HANDLE_VALUE,
        nullptr,
        0,
        static_cast<DWORD>(concurrentThreads)
    );

    if (m_hIOCP == nullptr) {
        printf("[IOCPCore] CreateIoCompletionPort failed: %lu\n", GetLastError());
        m_hIOCP = INVALID_HANDLE_VALUE;
        return false;
    }

    printf("[IOCPCore] Initialized (concurrentThreads: %u)\n", concurrentThreads);
    return true;
}

bool IOCPCore::Register(HANDLE handle, u64 completionKey) {
    if (!IsValid()) {
        printf("[IOCPCore] Not initialized\n");
        return false;
    }

    HANDLE result = CreateIoCompletionPort(
        handle,
        m_hIOCP,
        static_cast<ULONG_PTR>(completionKey),
        0
    );

    if (result != m_hIOCP) {
        printf("[IOCPCore] Register failed: %lu\n", GetLastError());
        return false;
    }

    return true;
}

std::optional<CompletionResult> IOCPCore::Dispatch(u32 timeoutMs) {
    if (!IsValid()) {
        return std::nullopt;
    }

    DWORD bytesTransferred = 0;
    ULONG_PTR completionKey = 0;
    OVERLAPPED* pOverlapped = nullptr;

    BOOL result = GetQueuedCompletionStatus(
        m_hIOCP,
        &bytesTransferred,
        &completionKey,
        &pOverlapped,
        static_cast<DWORD>(timeoutMs)
    );

    if (pOverlapped == nullptr) {
        if (!result) {
            DWORD error = GetLastError();
            if (error != WAIT_TIMEOUT) {
                printf("[IOCPCore] GetQueuedCompletionStatus error: %lu\n", error);
            }
        }
        return std::nullopt;
    }

    CompletionResult completion{};
    completion.completionKey   = static_cast<u64>(completionKey);
    completion.pOverlapped     = IOCPOverlapped::FromOverlapped(pOverlapped);
    completion.bytesTransferred = static_cast<u32>(bytesTransferred);
    completion.success         = (result == TRUE);

    return completion;
}

bool IOCPCore::PostCompletion(u64 completionKey, IOCPOverlapped* pOverlapped) {
    if (!IsValid()) {
        printf("[IOCPCore] Not initialized\n");
        return false;
    }

    BOOL result = PostQueuedCompletionStatus(
        m_hIOCP,
        0,
        static_cast<ULONG_PTR>(completionKey),
        pOverlapped ? reinterpret_cast<OVERLAPPED*>(pOverlapped) : nullptr
    );

    if (!result) {
        printf("[IOCPCore] PostQueuedCompletionStatus failed: %lu\n", GetLastError());
        return false;
    }

    return true;
}

void IOCPCore::Close() {
    if (IsValid()) {
        printf("[IOCPCore] Closing IOCP handle\n");
        CloseHandle(m_hIOCP);
        m_hIOCP = INVALID_HANDLE_VALUE;
    }
}

} // namespace IOCPFramework
