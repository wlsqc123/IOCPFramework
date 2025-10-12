#pragma once
#include <vector>
#include "ClientSession.h"
#include "../stdafx.h"

class IocpServer
{
public:
    IocpServer();
    ~IocpServer();

    bool Start(int port);
    void Stop();

private:
    void WorkerThread();
    void AcceptThread();
    
    bool _is_running;

    HANDLE _iocp_handle;
    SOCKET _listen_socket;
    std::vector<std::thread> _worker_threads;
    std::thread _accept_thread;
};