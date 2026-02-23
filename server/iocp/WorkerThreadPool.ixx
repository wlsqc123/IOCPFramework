module;

#include <Windows.h>
#include <functional>
#include <thread>
#include <vector>

export module worker_thread_pool;

import types;
import iocp_core;

export {
	using CompletionHandler = std::function<void(const CompletionResult &)>;

	// IOCPCore::dispatch()를 루프로 돌며 완료 통지를 수신하는 Worker Thread Pool
	class WorkerThreadPool
	{
	  public:
		// 종료 신호용 completionKey (일반 세션 ID와 구분되는 특수값)
		static constexpr uint64 SHUTDOWN_KEY = UINT64_MAX;

		WorkerThreadPool();
		~WorkerThreadPool();

		WorkerThreadPool(const WorkerThreadPool &) = delete;
		WorkerThreadPool &operator=(const WorkerThreadPool &) = delete;
		WorkerThreadPool(WorkerThreadPool &&) = delete;
		WorkerThreadPool &operator=(WorkerThreadPool &&) = delete;

		// threadCount == 0이면 hardware_concurrency() 사용
		bool start(IOCPCore &iocpCore, CompletionHandler handler, uint32 threadCount = 0);

		// m_running = false 후 모든 스레드 join. 빠른 종료는 SHUTDOWN_KEY를 직접 postCompletion할 것
		void stop();

		bool isRunning() const
		{
			return m_running;
		}

		uint32 threadCount() const
		{
			return static_cast<uint32>(m_threads.size());
		}

	  private:
		void workerLoop(IOCPCore *pIOCPCore, CompletionHandler handler);

		std::vector<std::thread> m_threads;
		std::atomic<bool>        m_running;
	};
}
