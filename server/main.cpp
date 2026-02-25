#include <WinSock2.h>
#include <cstdio>
#include <atomic>
#include <chrono>
#include <thread>
#include <utility>

import types;
import iocp_overlapped;
import iocp_core;
import worker_thread_pool;
import acceptor;

int main()
{
	printf("========================================\n");
	printf("IOCPFramework v0.1.0\n");
	printf("========================================\n\n");

	// 0. Winsock 초기화 — 모든 소켓 작업 전에 필요
	WSADATA wsaData{};

	if (0 != WSAStartup(MAKEWORD(2, 2), &wsaData))
	{
		printf("[FAIL] WSAStartup\n");

		return 1;
	}
	printf("[PASS] WSAStartup\n");

	IOCPCore iocp;

	if (false == iocp.init(0))
	{
		printf("[FAIL] IOCPCore Init\n");
		WSACleanup();

		return 1;
	}
	printf("[PASS] IOCPCore Init\n");

	// 1. Worker Thread Pool 기동 테스트
	std::atomic<int> handledCount = 0;
	std::atomic<bool> acceptCompleted = false;
	Acceptor acceptor;

	WorkerThreadPool pool;

	bool started = pool.start(iocp, [&](const CompletionResult &result)
	{
		if (nullptr != result.pOverlapped && IOOperation::ACCEPT == result.pOverlapped->operation)
		{
			acceptor.onAcceptComplete(result);

			return;
		}

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
		WSACleanup();

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

	if (2 != handledCount.load())
	{
		printf("[FAIL] Expected 2 handled completions (RECV+SEND), got %d\n", handledCount.load());

		pool.stop();
		WSACleanup();

		return 1;
	}
	printf("[PASS] All non-ACCEPT completions handled\n");

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
		WSACleanup();

		return 1;
	}
	printf("[PASS] WorkerThreadPool Stop\n");

	// 5. Acceptor 테스트 — 새 Pool 재기동 후 실제 TCP 연결 수용
	WorkerThreadPool pool2;

	bool started2 = pool2.start(iocp, [&](const CompletionResult &result)
	{
		if (nullptr != result.pOverlapped && IOOperation::ACCEPT == result.pOverlapped->operation)
		{
			acceptor.onAcceptComplete(result);

			return;
		}
	}, 2);

	if (false == started2)
	{
		printf("[FAIL] WorkerThreadPool2 Start\n");
		WSACleanup();

		return 1;
	}

	bool acceptorStarted = acceptor.start(iocp, 7777, [&](SOCKET clientSocket)
	{
		printf("[PASS] Accept completed — clientSocket: %llu\n",
			static_cast<uint64>(clientSocket));

		closesocket(clientSocket);
		acceptCompleted = true;
	});

	if (false == acceptorStarted)
	{
		printf("[FAIL] Acceptor Start\n");

		pool2.stop();
		WSACleanup();

		return 1;
	}
	printf("[PASS] Acceptor Start (port 7777)\n");

	// 자체 클라이언트 소켓으로 연결 시도
	SOCKET clientSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, 0);

	SOCKADDR_IN serverAddress{};
	serverAddress.sin_family      = AF_INET;
	serverAddress.sin_port        = htons(7777);
	serverAddress.sin_addr.s_addr = inet_addr("127.0.0.1");

	connect(clientSocket, reinterpret_cast<SOCKADDR *>(&serverAddress), sizeof(serverAddress));

	// Worker Thread가 ACCEPT 완료 통지를 처리할 시간 대기
	std::this_thread::sleep_for(std::chrono::milliseconds(500));

	closesocket(clientSocket);

	if (false == acceptCompleted.load())
	{
		printf("[FAIL] Accept not completed within timeout\n");

		acceptor.stop();

		uint32 count2 = pool2.threadCount();
		for (uint32 index = 0; index < count2; ++index)
		{
			iocp.postCompletion(WorkerThreadPool::SHUTDOWN_KEY);
		}

		pool2.stop();
		WSACleanup();

		return 1;
	}

	// 6. Acceptor + Pool 정리
	acceptor.stop();

	// ERROR_OPERATION_ABORTED 완료 통지 처리 시간 대기
	std::this_thread::sleep_for(std::chrono::milliseconds(200));

	uint32 count2 = pool2.threadCount();
	for (uint32 index = 0; index < count2; ++index)
	{
		iocp.postCompletion(WorkerThreadPool::SHUTDOWN_KEY);
	}

	pool2.stop();
	printf("[PASS] Acceptor Stop\n");

	WSACleanup();

	printf("\n========================================\n");
	printf("All tests passed!\n");
	printf("========================================\n");

	return 0;
}
