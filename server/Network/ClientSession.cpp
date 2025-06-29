#include "ClientSession.h"
#include <iostream>

ClientSession::ClientSession(SOCKET socket) : _socket(socket)
{
    memset(&_overlapped, 0, sizeof(OVERLAPPED));
    _wsaBuf.len = MAX_BUFFER_SIZE;
    _wsaBuf.buf = _buffer;
}

ClientSession::~ClientSession() { closesocket(_socket); }

void ClientSession::Recv()
{
    DWORD flags = 0;
    if (WSARecv(_socket, &_wsaBuf, 1, NULL, &flags, &_overlapped, NULL) == SOCKET_ERROR)
    {
        if (WSAGetLastError() != WSA_IO_PENDING)
        {
            std::cerr << "WSARecv failed: " << WSAGetLastError() << std::endl;
        }
    }
}

void ClientSession::Send(const char *message, int len)
{
    WSABUF wsaBuf;
    wsaBuf.buf = (char *)message;
    wsaBuf.len = len;
    DWORD sentBytes;

    if (WSASend(_socket, &wsaBuf, 1, &sentBytes, 0, nullptr, nullptr) == SOCKET_ERROR)
    {
        std::cerr << "WSASend failed: " << WSAGetLastError() << std::endl;
    }
}