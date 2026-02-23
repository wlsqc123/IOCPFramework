#include <cstdio>
#include <atomic>
#include <chrono>
#include <thread>
#include <utility>

import types;
import iocp_overlapped;
import iocp_core;
import worker_thread_pool;

int main()
{
	printf("========================================\n");
	printf("IOCPFramework v0.1.0\n");
	printf("========================================\n\n");

	IOCPCore iocp;

	if (false == iocp.init(0))
	{
		printf("[FAIL] IOCPCore Init\n");

		return 1;
	}
	printf("[PASS] IOCPCore Init\n");

	// 1. Worker Thread Pool 기동 테스트
	std::atomic<int> handledCount = 0;

	WorkerThreadPool pool;

	bool started = pool.start(iocp, [&](const CompletionResult &result)
	{
		printf("[Handler] key=%llu sessionId=%llu op=%d bytes=%u\n",
			result.completionKey,
			result.pOverlapped->sessionId,
			static_cast<int>(result.pOverlapped->operation),
			result.bytesTransferred);

		handledCount.fetch_add(1, std::memory_order_relaxed);
	}, 2);

	if (false == started)
	{
		printf("[FAIL] WorkerThreadPool Start\n");

		return 1;
	}
	printf("[PASS] WorkerThreadPool Start (%u threads)\n", pool.threadCount());

	// 2. 테스트 — 3개의 가짜 완료 패킷 전송
	IOCPOverlapped overlapped1;
	overlapped1.operation = IOOperation::RECV;
	overlapped1.sessionId = 1;

	IOCPOverlapped overlapped2;
	overlapped2.operation = IOOperation::SEND;
	overlapped2.sessionId = 2;

	IOCPOverlapped overlapped3;
	overlapped3.operation = IOOperation::ACCEPT;
	overlapped3.sessionId = 3;

	iocp.postCompletion(1, &overlapped1);
	iocp.postCompletion(2, &overlapped2);
	iocp.postCompletion(3, &overlapped3);

	// 3. 핸들러가 처리할 시간 대기
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	if (3 != handledCount.load())
	{
		printf("[FAIL] Expected 3 handled completions, got %d\n", handledCount.load());

		pool.stop();

		return 1;
	}
	printf("[PASS] All 3 completions handled\n");

	// 4. 종료 신호 전송 및 stop 테스트
	// threadCount()만큼 SHUTDOWN_KEY 전송 — 각 스레드가 하나씩 수신
	uint32 count = pool.threadCount();

	for (uint32 index = 0; index < count; ++index)
	{
		iocp.postCompletion(WorkerThreadPool::SHUTDOWN_KEY);
	}

	pool.stop();

	if (true == pool.isRunning())
	{
		printf("[FAIL] Pool should be stopped\n");

		return 1;
	}
	printf("[PASS] WorkerThreadPool Stop\n");

	printf("\n========================================\n");
	printf("All WorkerThreadPool tests passed!\n");
	printf("========================================\n");

	return 0;
}
