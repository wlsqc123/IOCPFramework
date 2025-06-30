#include <algorithm>
#include <atomic> // atomic 사용을 위한 헤더
#include <chrono>
#include <csignal> // 신호 처리를 위한 헤더
#include <iostream>
#include <mutex> // 뮤텍스 사용을 위한 헤더
#include <random>
#include <string>
#include <thread>
#include <vector>
#include <winsock2.h>
#include <ws2tcpip.h>

#pragma comment(lib, "ws2_32.lib")

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 9000
#define BUFFER_SIZE 1024
#define RANDOM_STRING_LENGTH 32 // 무작위 문자열 길이
#define NUM_CLIENTS 10000 // 생성할 클라이언트 수

// 공유 자원
std::mutex console_mutex;
std::atomic<bool> is_running(true); // 프로그램 실행 상태를 관리하는 atomic 변수

// Ctrl+C 신호를 처리하기 위한 핸들러
void signal_handler(int signal)
{
    if (signal == SIGINT)
    {
        std::cout << "\nCtrl+C detected. Shutting down clients..." << std::endl;
        is_running = false;
    }
}

// 무작위 문자열을 생성하는 함수
std::string generate_random_string(int length)
{
    const std::string charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    thread_local std::mt19937 gen(std::random_device{}() + std::hash<std::thread::id>{}(std::this_thread::get_id()));
    std::uniform_int_distribution<int> distrib(0, charset.length() - 1);
    std::string random_string;
    random_string.reserve(length);
    for (int i = 0; i < length; ++i)
    {
        random_string += charset[distrib(gen)];
    }
    return random_string;
}

// 각 클라이언트의 동작을 정의하는 함수
void client_task(int client_id)
{
    SOCKET client_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (client_socket == INVALID_SOCKET)
    {
        std::lock_guard<std::mutex> lock(console_mutex);
        std::cerr << "[Client " << client_id << "] Socket creation failed: " << WSAGetLastError() << std::endl;
        return;
    }

    SOCKADDR_IN serverAddr;
    memset(&serverAddr, 0, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr);

    if (connect(client_socket, (SOCKADDR *)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR)
    {
        std::lock_guard<std::mutex> lock(console_mutex);
        std::cerr << "[Client " << client_id << "] Connect failed: " << WSAGetLastError() << std::endl;
        closesocket(client_socket);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(console_mutex);
        std::cout << "[Client " << client_id << "] Connected and starting sending messages." << std::endl;
    }

    char recv_buffer[BUFFER_SIZE];

    // is_running 플래그가 true인 동안 무한 반복
    while (is_running)
    {
        std::string send_buffer = generate_random_string(RANDOM_STRING_LENGTH);

        if (send(client_socket, send_buffer.c_str(), (int)send_buffer.length(), 0) == SOCKET_ERROR)
        {
            if (is_running)
            { // 정상 종료가 아닐 경우에만 에러 메시지 출력
                std::lock_guard<std::mutex> lock(console_mutex);
                std::cerr << "[Client " << client_id << "] Send failed: " << WSAGetLastError() << std::endl;
            }
            break;
        }

        int recvBytes = recv(client_socket, recv_buffer, BUFFER_SIZE - 1, 0);
        if (recvBytes <= 0)
        {
            if (is_running)
            { // 정상 종료가 아닐 경우
                std::lock_guard<std::mutex> lock(console_mutex);
                if (recvBytes == 0)
                {
                    std::cout << "[Client " << client_id << "] Server closed the connection." << std::endl;
                }
                else
                {
                    std::cerr << "[Client " << client_id << "] Recv failed: " << WSAGetLastError() << std::endl;
                }
            }
            break;
        }

        // 0.1초 대기
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }

    closesocket(client_socket);
    {
        std::lock_guard<std::mutex> lock(console_mutex);
        std::cout << "[Client " << client_id << "] Connection closed." << std::endl;
    }
}

int main()
{
    // 윈속 초기화
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
    {
        std::cerr << "WSAStartup failed: " << WSAGetLastError() << std::endl;
        return 1;
    }

    // Ctrl+C 신호 처리기 등록
    signal(SIGINT, signal_handler);

    std::cout << "Creating " << NUM_CLIENTS << " clients for stress test..." << std::endl;
    std::cout << "Press Ctrl+C to stop the test." << std::endl;

    std::vector<std::thread> client_threads;
    for (int i = 0; i < NUM_CLIENTS; ++i)
    {
        client_threads.emplace_back(client_task, i + 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // 동시 접속 부하를 줄이기 위한 약간의 딜레이
    }

    for (auto &th : client_threads)
    {
        if (th.joinable())
        {
            th.join();
        }
    }

    // 윈속 정리
    WSACleanup();

    std::cout << "Stress test finished." << std::endl;

    return 0;
}