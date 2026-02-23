module;

#include <WinSock2.h>
#include <Windows.h>
#include <cstdio>
#include <optional>

module iocp_core;

IOCPCore::IOCPCore() : m_hIOCP(INVALID_HANDLE_VALUE)
{
}

IOCPCore::~IOCPCore()
{
	close();
}

IOCPCore::IOCPCore(IOCPCore &&other) noexcept : m_hIOCP(other.m_hIOCP)
{
	other.m_hIOCP = INVALID_HANDLE_VALUE;
}

IOCPCore &IOCPCore::operator=(IOCPCore &&other) noexcept
{
	if (this != &other)
	{
		close();

		m_hIOCP = other.m_hIOCP;
		other.m_hIOCP = INVALID_HANDLE_VALUE;
	}

	return *this;
}

bool IOCPCore::init(uint32 concurrentThreads)
{
	if (true == isValid())
	{
		printf("[IOCPCore] Already initialized\n");

		return false;
	}

	m_hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, static_cast<DWORD>(concurrentThreads));

	if (m_hIOCP == nullptr)
	{
		printf("[IOCPCore] CreateIoCompletionPort failed: %lu\n", GetLastError());

		m_hIOCP = INVALID_HANDLE_VALUE;

		return false;
	}

	printf("[IOCPCore] Initialized (concurrentThreads: %u)\n", concurrentThreads);

	return true;
}

bool IOCPCore::registerHandle(HANDLE handle, uint64 completionKey)
{
	if (false == isValid())
	{
		printf("[IOCPCore] Not initialized\n");

		return false;
	}

	HANDLE result = CreateIoCompletionPort(handle, m_hIOCP, static_cast<ULONG_PTR>(completionKey), 0);

	if (result != m_hIOCP)
	{
		printf("[IOCPCore] Register failed: %lu\n", GetLastError());

		return false;
	}

	return true;
}

std::optional<CompletionResult> IOCPCore::dispatch(uint32 timeoutMs)
{
	if (false == isValid())
	{
		return std::nullopt;
	}

	DWORD bytesTransferred = 0;
	ULONG_PTR completionKey = 0;
	OVERLAPPED *pOverlapped = nullptr;

	BOOL result = GetQueuedCompletionStatus(m_hIOCP, &bytesTransferred, &completionKey, &pOverlapped, static_cast<DWORD>(timeoutMs));

	if (nullptr == pOverlapped)
	{
		if (false == result)
		{
			DWORD error = GetLastError();

			if (WAIT_TIMEOUT != error)
			{
				printf("[IOCPCore] GetQueuedCompletionStatus error: %lu\n", error);
			}
		}

		return std::nullopt;
	}

	CompletionResult completion{};
	completion.completionKey = static_cast<uint64>(completionKey);
	completion.pOverlapped = IOCPOverlapped::fromOverlapped(pOverlapped);
	completion.bytesTransferred = static_cast<uint32>(bytesTransferred);
	completion.success = (TRUE == result);
	completion.errorCode = (TRUE == result) ? 0 : GetLastError();

	return completion;
}

bool IOCPCore::postCompletion(uint64 completionKey, IOCPOverlapped *pOverlapped)
{
	if (false == isValid())
	{
		printf("[IOCPCore] Not initialized\n");

		return false;
	}

	BOOL result = PostQueuedCompletionStatus(m_hIOCP, 0, static_cast<ULONG_PTR>(completionKey),
											pOverlapped ? reinterpret_cast<OVERLAPPED *>(pOverlapped) : nullptr);

	if (false == result)
	{
		printf("[IOCPCore] PostQueuedCompletionStatus failed: %lu\n", GetLastError());
		
		return false;
	}

	return true;
}

void IOCPCore::close()
{
	if (true == isValid())
	{
		printf("[IOCPCore] Closing IOCP handle\n");

		CloseHandle(m_hIOCP);

		m_hIOCP = INVALID_HANDLE_VALUE;
	}
}
