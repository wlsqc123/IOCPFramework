module;

#include <WinSock2.h>
#include <MSWSock.h>
#include <Windows.h>
#include <atomic>
#include <functional>
#include <memory>

export module acceptor;

import types;
import iocp_overlapped;
import iocp_core;

export {
	// AcceptEx 주소 버퍼 크기: (sizeof(SOCKADDR_IN) + 16) * 2 = 64 bytes (IPv4 기준)
	constexpr uint32 ACCEPT_ADDR_BUFFER_SIZE = (sizeof(SOCKADDR_IN) + 16) * 2;

	// AcceptEx 1건에 필요한 컨텍스트
	// IOCPOverlapped::ownerPtr에 this를 보관
	struct AcceptContext
	{
		AcceptContext();
		~AcceptContext();

		// 복사/이동 금지 — ownerPtr이 this를 가리키므로 주소 고정 필요
		AcceptContext(const AcceptContext &) = delete;
		AcceptContext &operator=(const AcceptContext &) = delete;
		AcceptContext(AcceptContext &&) = delete;
		AcceptContext &operator=(AcceptContext &&) = delete;

		IOCPOverlapped overlappedContext;				// 반드시 첫번째 멤버
		SOCKET acceptSocket = INVALID_SOCKET;			// AcceptEx에 미리 생성해 두는 클라이언트 소켓
		char addressBuffer[ACCEPT_ADDR_BUFFER_SIZE];	// 로컬/원격 주소 저장 공간
	};

	// accept 완료 시 호출되는 콜백 (클라이언트 소켓 소유권은 호출자에게 이전)
	using AcceptHandler = std::function<void(SOCKET clientSocket)>;

	// AcceptEx 기반 비동기 Accept 관리자
	class Acceptor
	{
	  public:
		Acceptor();
		~Acceptor();

		Acceptor(const Acceptor &) = delete;
		Acceptor &operator=(const Acceptor &) = delete;
		Acceptor(Acceptor &&) = delete;
		Acceptor &operator=(Acceptor &&) = delete;

		// Listen 소켓 생성 + bind + listen + IOCP 등록 + AcceptEx 최초 발행
		bool start(IOCPCore &iocpCore, uint16 port, AcceptHandler onAccept);

		// Listen 소켓 닫기 (진행 중인 AcceptEx는 ERROR_OPERATION_ABORTED(995)로 완료)
		void stop();

		// Worker Thread의 CompletionHandler에서 IOOperation::ACCEPT 수신 시 호출
		void onAcceptComplete(const CompletionResult &result);

		bool isListening() const;

	  private:
		bool initListenSocket(uint16 port);
		bool loadAcceptEx();
		bool issueAcceptEx();

		bool   bindAndListen(SOCKET socket, uint16 port);
		SOCKET createAcceptSocket();
		void   closeListenSocket();

		SOCKET			m_listenSocket = INVALID_SOCKET;
		LPFN_ACCEPTEX	m_lpfnAcceptEx = nullptr;
		IOCPCore	   *m_pIOCPCore = nullptr;
		AcceptHandler	m_acceptHandler;

		std::unique_ptr<AcceptContext>	m_acceptContext;
		std::atomic<bool>				m_listening = false;
	};
}
