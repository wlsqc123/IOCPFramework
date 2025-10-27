#include "stdafx.h"
#include <iostream>
#include "./core/network/IocpServer.h"
import config.network;

int main()
{
    IocpServer server;
    if (server.Start(Config::Network::DEFAULT_SERVER_PORT))
    {
        // 서버가 시작된 이후의 로직 (예: 콘솔 입력)
        std::cout << "Press Enter to stop the server..." << std::endl;
        std::cin.get();
        server.Stop();
    }

    return 0;
}