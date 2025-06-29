#include <iostream>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h> 

#pragma comment(lib, "ws2_32.lib")

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 9000
#define BUFFER_SIZE 1024

int main()
{
    // 1. ���� �ʱ�ȭ
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
    {
        std::cerr << "WSAStartup failed with error: " << WSAGetLastError() << std::endl;
        return 1;
    }

    // 2. ���� ����
    SOCKET clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (clientSocket == INVALID_SOCKET)
    {
        std::cerr << "Socket creation failed with error: " << WSAGetLastError() << std::endl;
        WSACleanup();
        return 1;
    }

    // 3. ���� �ּ� ����
    SOCKADDR_IN serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);

    if (0 >= inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr))
    {
        std::cerr << "Invalid address/ Address not supported" << std::endl;
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    // 4. ������ ����
    if (SOCKET_ERROR == connect(clientSocket, (SOCKADDR *)&serverAddr, sizeof(serverAddr)))
    {
        std::cerr << "Connect failed with error: " << WSAGetLastError() << std::endl;
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }

    std::cout << "Successfully connected to server on " << SERVER_IP << ":" << SERVER_PORT << std::endl;
    std::cout << "Enter message to send (type 'exit' to quit): " << std::endl;

    char recvBuffer[BUFFER_SIZE];
    std::string sendBuffer;

    // 5. ������ �ۼ��� ����
    while (true)
    {
        std::cout << "> ";
        std::getline(std::cin, sendBuffer);

        if (sendBuffer.empty())
        {
            continue;
        }

        if (sendBuffer == "exit")
        {
            break;
        }

        // ������ �޽��� ����
        int sendBytes = send(clientSocket, sendBuffer.c_str(), (int)sendBuffer.length(), 0);
        if (sendBytes == SOCKET_ERROR)
        {
            std::cerr << "Send failed with error: " << WSAGetLastError() << std::endl;
            break;
        }

        // �����κ��� ���� �޽��� ����
        int recvBytes = recv(clientSocket, recvBuffer, BUFFER_SIZE - 1, 0);
        if (recvBytes > 0)
        {
            recvBuffer[recvBytes] = '\0'; // Null-terminate the string
            std::cout << "Echo from server: " << recvBuffer << std::endl;
        }
        else if (recvBytes == 0)
        {
            std::cout << "Server closed the connection." << std::endl;
            break;
        }
        else
        {
            std::cerr << "Recv failed with error: " << WSAGetLastError() << std::endl;
            break;
        }
    }

    // 6. ���� ���� �� ���� ����
    closesocket(clientSocket);
    WSACleanup();

    std::cout << "Connection closed. Press Enter to exit." << std::endl;
    std::cin.get();

    return 0;
}