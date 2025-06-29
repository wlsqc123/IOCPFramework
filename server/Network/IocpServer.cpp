#include "stdafx.h" 
#include "IocpServer.h"
#include <iostream>

IocpServer::IocpServer() : _iocp_handle(NULL), _listen_socket(INVALID_SOCKET), _is_running(false) {}

IocpServer::~IocpServer() { Stop(); }

bool IocpServer::Start(int port)
{
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed" << std::endl;
        return false;
    }

    _iocp_handle = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (_iocp_handle == NULL)
    {
        std::cerr << "CreateIoCompletionPort failed" << std::endl;
        return false;
    }

    SYSTEM_INFO sysInfo;
    GetSystemInfo(&sysInfo);
    for (DWORD i = 0; i < sysInfo.dwNumberOfProcessors * 2; ++i)
    {
        _worker_threads.emplace_back(&IocpServer::WorkerThread, this);
    }

    _listen_socket = WSASocket(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (_listen_socket == INVALID_SOCKET)
    {
        std::cerr << "WSASocket failed" << std::endl;
        return false;
    }

    SOCKADDR_IN serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(_listen_socket, (SOCKADDR *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        std::cerr << "bind failed" << std::endl;
        return false;
    }

    if (listen(_listen_socket, SOMAXCONN) == SOCKET_ERROR)
    {
        std::cerr << "listen failed" << std::endl;
        return false;
    }

    _is_running = true;
    _accept_thread = std::thread(&IocpServer::AcceptThread, this);

    std::cout << "Server started on port " << port << std::endl;
    return true;
}

void IocpServer::Stop()
{
    _is_running = false;
    closesocket(_listen_socket);
    CloseHandle(_iocp_handle);
    WSACleanup();

    if (_accept_thread.joinable())
    {
        _accept_thread.join();
    }
    for (auto &th : _worker_threads)
    {
        if (th.joinable())
        {
            th.join();
        }
    }
}

void IocpServer::WorkerThread()
{
    DWORD bytesTransferred;
    ClientSession *clientSession;
    OVERLAPPED *overlapped;

    while (_is_running)
    {
        BOOL result = GetQueuedCompletionStatus(_iocp_handle, &bytesTransferred, (PULONG_PTR)&clientSession, &overlapped,
                                                INFINITE);

        if (!result || bytesTransferred == 0)
        {
            if (clientSession)
            {
                std::cout << "Client disconnected" << std::endl;
                delete clientSession;
            }
            continue;
        }

        std::cout << "Received: " << std::string(clientSession->GetBuffer(), bytesTransferred) << std::endl;

        // Echo
        clientSession->Send(clientSession->GetBuffer(), bytesTransferred);
        clientSession->Recv();
    }
}

void IocpServer::AcceptThread()
{
    while (_is_running)
    {
        SOCKET clientSocket = accept(_listen_socket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET)
        {
            if (!_is_running)
                break;
            std::cerr << "accept failed" << std::endl;
            continue;
        }

        std::cout << "Client connected" << std::endl;

        ClientSession *newSession = new ClientSession(clientSocket);
        CreateIoCompletionPort((HANDLE)clientSocket, _iocp_handle, (ULONG_PTR)newSession, 0);
        newSession->Recv();
    }
}