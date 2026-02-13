#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdio>
#include <cstring>

#pragma comment(lib, "ws2_32.lib")

int main() {
    printf("========================================\n");
    printf("IOCP Client v0.1.0\n");
    printf("========================================\n\n");

    // Winsock 초기화
    WSADATA wsaData;
    if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        printf("WSAStartup failed!\n");
        return 1;
    }

    // 소켓 생성
    SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock == INVALID_SOCKET) {
        printf("Socket creation failed!\n");
        WSACleanup();
        return 1;
    }

    // 서버 주소 설정
    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(9000);
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

    // 서버 연결
    printf("Connecting to 127.0.0.1:9000...\n");
    if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        printf("Connection failed! (Server might not be running)\n");
        closesocket(sock);
        WSACleanup();
        return 1;
    }

    printf("Connected to server!\n\n");

    // 메시지 전송
    const char* msg = "Hello Server";
    printf("Sending: %s\n", msg);
    send(sock, msg, static_cast<int>(strlen(msg)), 0);

    // 응답 수신 (타임아웃 설정)
    DWORD timeout = 3000; // 3초
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout));

    char buffer[1024] = {};
    int received = recv(sock, buffer, sizeof(buffer) - 1, 0);

    if (received > 0) {
        buffer[received] = '\0';
        printf("Received: %s\n", buffer);
    } else if (received == 0) {
        printf("Server closed connection.\n");
    } else {
        printf("No response from server (timeout or error).\n");
    }

    printf("\nPress Enter to exit...\n");
    getchar();

    // 정리
    closesocket(sock);
    WSACleanup();
    return 0;
}
