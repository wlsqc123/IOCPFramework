module;

#include <WinSock2.h>
#include <Windows.h>

export module iocp_overlapped;

import types;

export {
	enum class IOOperation : uint8
	{
		NONE		= 0,
		RECV		= 1,
		SEND		= 2,
		ACCEPT		= 3,
		DISCONNECT	= 4,
	};

	struct IOCPOverlapped
	{
		WSAOVERLAPPED	overlapped;	// !!반드시 첫 번째 멤버!! - 위치 수정 X
		SessionId		sessionId;	// 소유 세션 ID (0 = 미할당)
		void			*ownerPtr;	// 추가 컨텍스트
		IOOperation		operation;

		IOCPOverlapped()
		{
			reset();
		}

		void reset()
		{
			ZeroMemory(&overlapped, sizeof(WSAOVERLAPPED));
			operation = IOOperation::NONE;
			sessionId = 0;
			ownerPtr = nullptr;
		}

		static IOCPOverlapped *fromOverlapped(OVERLAPPED *pOverlapped)
		{
			return reinterpret_cast<IOCPOverlapped *>(pOverlapped);
		}
	};
}
