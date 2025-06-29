#include <iostream>
#include "./Network/IocpServer.h"

int main()
{
    IocpServer server;
    if (server.Start(9000))
    {
        // 서버가 종료될 때까지 대기 (예: 콘솔 입력)
        std::cout << "Press Enter to stop the server..." << std::endl;
        std::cin.get();
        server.Stop();
    }

    return 0;
}