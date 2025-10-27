#pragma once
#include "../stdafx.h"
import config.network;

// �� Ŭ���̾�Ʈ���� ������ ����. Ŭ���̾�Ʈ ����, ������ ����, I/O �۾��� ó��.

class ClientSession
{
public:
    ClientSession(SOCKET socket);
    ~ClientSession();

    void Recv();
    void Send(const char *message, int len);


public:
    // Getter
    SOCKET              get_socket() const      { return _socket; }
    const char         *get_buffer() const      { return _buffer; }
    const OVERLAPPED&   get_overlapped() const  { return _overlapped; }

private:
    SOCKET      _socket;
    OVERLAPPED  _overlapped;
    WSABUF      _wsa_buf;
    char        _buffer[Config::Network::MAX_BUFFER_SIZE];
};