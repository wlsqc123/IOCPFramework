#include <iostream>
#include "./Network/IocpServer.h"

int main()
{
    IocpServer server;
    if (server.Start(9000))
    {
        // ������ ����� ������ ��� (��: �ܼ� �Է�)
        std::cout << "Press Enter to stop the server..." << std::endl;
        std::cin.get();
        server.Stop();
    }

    return 0;
}