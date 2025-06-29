#pragma once
#include "../stdafx.h"

#define MAX_BUFFER_SIZE 1024

// �� Ŭ���̾�Ʈ���� ������ ����. Ŭ���̾�Ʈ ����, ������ ����, I/O �۾��� ó��.

class ClientSession
{
public:
    ClientSession(SOCKET socket);
    ~ClientSession();

    void Recv();
    void Send(const char *message, int len);

    SOCKET GetSocket() const { return _socket; }
    char *GetBuffer() { return _buffer; }
    OVERLAPPED *GetOverlapped() { return &_overlapped; }

private:
    SOCKET _socket;
    char _buffer[MAX_BUFFER_SIZE];
    WSABUF _wsaBuf;
    OVERLAPPED _overlapped;
};