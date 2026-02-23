module;

#include <Windows.h>
#include <cstdio>
#include <functional>
#include <thread>
#include <vector>

module worker_thread_pool;

WorkerThreadPool::WorkerThreadPool() : m_running(false)
{
}

WorkerThreadPool::~WorkerThreadPool()
{
	stop();
}

bool WorkerThreadPool::start(IOCPCore &iocpCore, CompletionHandler handler, uint32 threadCount)
{
	if (true == m_running)
	{
		printf("[WorkerThreadPool] Already running\n");

		return false;
	}

	if (false == iocpCore.isValid())
	{
		printf("[WorkerThreadPool] IOCPCore is not initialized\n");

		return false;
	}

	if (0 == threadCount)
	{
		threadCount = static_cast<uint32>(std::thread::hardware_concurrency());

		if (0 == threadCount)
		{
			threadCount = 1;
		}
	}

	m_running = true;

	m_threads.reserve(threadCount);

	for (uint32 index = 0; index < threadCount; ++index)
	{
		m_threads.emplace_back(&WorkerThreadPool::workerLoop, this, &iocpCore, handler);
	}

	printf("[WorkerThreadPool] Started with %u thread(s)\n", threadCount);

	return true;
}

void WorkerThreadPool::stop()
{
	if (false == m_running)
	{
		return;
	}

	m_running = false;

	// dispatch 타임아웃(1초)마다 m_running을 재확인하여 자연 종료
	// 즉시 종료가 필요하면 호출 측에서 threadCount()만큼 SHUTDOWN_KEY를 postCompletion할 것
	for (std::thread &thread : m_threads)
	{
		if (true == thread.joinable())
		{
			thread.join();
		}
	}

	m_threads.clear();

	printf("[WorkerThreadPool] Stopped\n");
}

void WorkerThreadPool::workerLoop(IOCPCore *pIOCPCore, CompletionHandler handler)
{
	printf("[WorkerThreadPool] Thread started (id: %lu)\n", GetCurrentThreadId());

	while (true == m_running)
	{
		std::optional<CompletionResult> result = pIOCPCore->dispatch();

		if (false == result.has_value())
		{
			continue;
		}

		if (SHUTDOWN_KEY == result->completionKey)
		{
			printf("[WorkerThreadPool] Shutdown signal received (thread: %lu)\n", GetCurrentThreadId());

			break;
		}

		handler(*result);
	}

	printf("[WorkerThreadPool] Thread exiting (id: %lu)\n", GetCurrentThreadId());
}
