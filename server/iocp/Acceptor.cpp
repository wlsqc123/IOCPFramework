module;

#include <WinSock2.h>
#include <MSWSock.h>
#include <Windows.h>
#include <cstdio>
#include <memory>

module acceptor;

// ====================================================
// AcceptContext
// ====================================================

AcceptContext::AcceptContext()
{
	overlappedContext.reset();
	overlappedContext.operation = IOOperation::ACCEPT;
	overlappedContext.ownerPtr  = this;

	ZeroMemory(addressBuffer, sizeof(addressBuffer));

	acceptSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);
}

AcceptContext::~AcceptContext()
{
	if (INVALID_SOCKET != acceptSocket)
	{
		closesocket(acceptSocket);
		acceptSocket = INVALID_SOCKET;
	}
}

// ====================================================
// Acceptor
// ====================================================

Acceptor::Acceptor()
{
}

Acceptor::~Acceptor()
{
	stop();
}

// === Orchestrator ===

bool Acceptor::start(IOCPCore &iocpCore, uint16 port, AcceptHandler onAccept)
{
	if (true == m_listening)
	{
		printf("[Acceptor] Already listening\n");

		return false;
	}

	m_pIOCPCore = &iocpCore;
	m_acceptHandler  = std::move(onAccept);

	if (false == initListenSocket(port))
	{
		return false;
	}

	if (false == loadAcceptEx())
	{
		closeListenSocket();

		return false;
	}

	m_listening = true;

	if (false == issueAcceptEx())
	{
		m_listening = false;
		closeListenSocket();

		return false;
	}

	printf("[Acceptor] Listening on port %u\n", port);

	return true;
}

void Acceptor::stop()
{
	if (false == m_listening)
	{
		return;
	}

	m_listening = false;

	// closesocket 호출 → 진행 중인 AcceptEx가 ERROR_OPERATION_ABORTED(995)로 완료 통지됨
	// m_acceptContext는 onAcceptComplete가 호출될 때까지 유지되어야 함
	closeListenSocket();

	printf("[Acceptor] Stopped\n");
}

void Acceptor::onAcceptComplete(const CompletionResult &result)
{
	if (false == result.success)
	{
		if (ERROR_OPERATION_ABORTED == result.errorCode)
		{
			// stop() 호출로 인한 정상 취소 — 재발행 X
			printf("[Acceptor] Accept cancelled (stopping)\n");
			m_acceptContext.reset();

			return;
		}

		printf("[Acceptor] Accept error: %u — reissuing\n", result.errorCode);

		m_acceptContext.reset();

		if (true == m_listening)
		{
			issueAcceptEx();
		}

		return;
	}

	SOCKET clientSocket = m_acceptContext->acceptSocket;

	// 소유권 이전 전에 accept 소켓 무효화 (AcceptContext 소멸 시 closesocket 방지)
	m_acceptContext->acceptSocket = INVALID_SOCKET;

	// listen 소켓의 속성(로컬 주소 등)을 클라이언트 소켓에 상속
	setsockopt(clientSocket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT, reinterpret_cast<char *>(&m_listenSocket), sizeof(m_listenSocket));

	m_acceptHandler(clientSocket);

	m_acceptContext.reset();

	if (true == m_listening)
	{
		issueAcceptEx();
	}
}

bool Acceptor::initListenSocket(uint16 port)
{
	m_listenSocket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, nullptr, 0, WSA_FLAG_OVERLAPPED);

	if (INVALID_SOCKET == m_listenSocket)
	{
		printf("[Acceptor] WSASocket failed: %d\n", WSAGetLastError());

		return false;
	}

	if (false == bindAndListen(m_listenSocket, port))
	{
		closeListenSocket();

		return false;
	}

	if (false == m_pIOCPCore->registerHandle(reinterpret_cast<HANDLE>(m_listenSocket), 0))
	{
		printf("[Acceptor] registerHandle failed\n");
		closeListenSocket();

		return false;
	}

	return true;
}

bool Acceptor::loadAcceptEx()
{
	GUID guidAcceptEx  = WSAID_ACCEPTEX;
	DWORD bytesReturned = 0;

	int result = WSAIoctl(
		m_listenSocket, SIO_GET_EXTENSION_FUNCTION_POINTER,
		&guidAcceptEx, sizeof(guidAcceptEx),
		&m_lpfnAcceptEx, sizeof(m_lpfnAcceptEx),
		&bytesReturned,
		nullptr,
		nullptr
	);

	if (SOCKET_ERROR == result)
	{
		printf("[Acceptor] WSAIoctl(AcceptEx) failed: %d\n", WSAGetLastError());

		return false;
	}

	return true;
}

bool Acceptor::issueAcceptEx()
{
	m_acceptContext = std::make_unique<AcceptContext>();

	if (INVALID_SOCKET == m_acceptContext->acceptSocket)
	{
		printf("[Acceptor] createAcceptSocket failed: %d\n", WSAGetLastError());
		m_acceptContext.reset();

		return false;
	}

	DWORD bytesReceived = 0;

	BOOL result = m_lpfnAcceptEx(
		m_listenSocket,
		m_acceptContext->acceptSocket, m_acceptContext->addressBuffer,
		0,							// dwReceiveDataLength = 0 (첫 수신 데이터 없음)
		sizeof(SOCKADDR_IN) + 16, sizeof(SOCKADDR_IN) + 16,
		&bytesReceived, &m_acceptContext->overlappedContext.overlapped
	);

	if (FALSE == result)
	{
		int error = WSAGetLastError();

		if (WSA_IO_PENDING != error)
		{
			printf("[Acceptor] AcceptEx failed: %d\n", error);
			m_acceptContext.reset();

			return false;
		}

		// WSA_IO_PENDING → 정상. 완료 통지는 IOCP를 통해 수신됨
	}

	return true;
}

bool Acceptor::bindAndListen(SOCKET socket, uint16 port)
{
	SOCKADDR_IN address{};
	address.sin_family      = AF_INET;
	address.sin_port        = htons(port);
	address.sin_addr.s_addr = INADDR_ANY;

	if (SOCKET_ERROR == bind(socket, reinterpret_cast<SOCKADDR *>(&address), sizeof(address)))
	{
		printf("[Acceptor] bind failed: %d\n", WSAGetLastError());

		return false;
	}

	if (SOCKET_ERROR == listen(socket, SOMAXCONN))
	{
		printf("[Acceptor] listen failed: %d\n", WSAGetLastError());

		return false;
	}

	return true;
}

void Acceptor::closeListenSocket()
{
	if (INVALID_SOCKET == m_listenSocket)
	{
		return;
	}

	closesocket(m_listenSocket);
	m_listenSocket = INVALID_SOCKET;
}

bool Acceptor::isListening() const
{
	return INVALID_SOCKET != m_listenSocket;
}