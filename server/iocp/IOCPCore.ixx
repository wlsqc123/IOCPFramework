module;

#include <WinSock2.h>
#include <Windows.h>
#include <optional>

export module iocp_core;

import types;
import iocp_overlapped;

export {
	// GetQueuedCompletionStatus 결과
	struct CompletionResult
	{
		uint64			completionKey;		// 등록 시 부여한 고유 식별자 (Session ID)
		IOCPOverlapped	*pOverlapped;		// 완료된 작업의 컨텍스트 (작업 유형, 세션/버퍼 정보)
		uint32			bytesTransferred;	// 실제 송수신된 바이트 수
		uint32			errorCode;			// IO 실패 시 GetLastError() 값 (success == true이면 0)
		bool			success;			// IO 작업 성공 여부
	};

	// IOCP CompletionPort RAII 래퍼
	class IOCPCore
	{
	  public:
		IOCPCore();
		~IOCPCore();

		IOCPCore(const IOCPCore &) = delete;
		IOCPCore &operator=(const IOCPCore &) = delete;
		IOCPCore(IOCPCore &&other) noexcept;
		IOCPCore &operator=(IOCPCore &&other) noexcept;

		// IOCP 포트 생성 
		bool init(uint32 concurrentThreads = 0); // (concurrentThreads: 0 = CPU 코어 수)

		// 소켓/핸들을 IOCP에 등록
		bool registerHandle(HANDLE handle, uint64 completionKey);

		// 완료 통지 대기 (타임아웃 시 nullopt)
		std::optional<CompletionResult> dispatch(uint32 timeoutMs = INFINITE);

		// 사용자 정의 완료 패킷 전송 (예: 종료 신호)
		bool postCompletion(uint64 completionKey, IOCPOverlapped *pOverlapped = nullptr);

		HANDLE getHandle() const
		{
			return m_hIOCP;
		}

		bool isValid() const
		{
			return INVALID_HANDLE_VALUE != m_hIOCP && nullptr != m_hIOCP;
		}

	  private:
		HANDLE m_hIOCP;

		void close();
	};
}
