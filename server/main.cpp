#include "iocp/IOCPCore.h"
#include <cstdio>

using namespace IOCPFramework;

int main() {
    printf("========================================\n");
    printf("IOCPFramework v0.1.0\n");
    printf("========================================\n\n");

    // 1. IOCP 생성 테스트
    IOCPCore iocp;
    if (!iocp.Init(0)) {
        printf("[FAIL] IOCPCore Init\n");
        return 1;
    }
    printf("[PASS] IOCPCore Init\n");

    // 2. 유효성 테스트
    if (!iocp.IsValid()) {
        printf("[FAIL] IsValid\n");
        return 1;
    }
    printf("[PASS] IsValid\n");

    // 3. PostCompletion -> Dispatch 테스트
    IOCPOverlapped testOv;
    testOv.operation = IOOperation::RECV;
    testOv.sessionId = 42;

    if (!iocp.PostCompletion(100, &testOv)) {
        printf("[FAIL] PostCompletion\n");
        return 1;
    }
    printf("[PASS] PostCompletion\n");

    auto result = iocp.Dispatch(1000);
    if (!result.has_value()) {
        printf("[FAIL] Dispatch returned nullopt\n");
        return 1;
    }

    if (result->completionKey != 100 ||
        result->pOverlapped->sessionId != 42 ||
        result->pOverlapped->operation != IOOperation::RECV) {
        printf("[FAIL] Dispatch data mismatch\n");
        return 1;
    }
    printf("[PASS] Dispatch (key=%llu, sessionId=%llu, op=RECV)\n",
           result->completionKey, result->pOverlapped->sessionId);

    // 4. Move semantics 테스트
    IOCPCore iocp2 = std::move(iocp);
    if (iocp.IsValid()) {
        printf("[FAIL] Original should be invalid after move\n");
        return 1;
    }
    if (!iocp2.IsValid()) {
        printf("[FAIL] Moved target should be valid\n");
        return 1;
    }
    printf("[PASS] Move semantics\n");

    // 5. Dispatch 타임아웃 테스트
    auto timeout = iocp2.Dispatch(0);
    if (timeout.has_value()) {
        printf("[FAIL] Empty queue should return nullopt\n");
        return 1;
    }
    printf("[PASS] Dispatch timeout\n");

    printf("\n========================================\n");
    printf("All IOCPCore tests passed!\n");
    printf("========================================\n");

    return 0;
}
