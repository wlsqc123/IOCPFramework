#include <algorithm>
#include <atomic> // atomic ����� ���� ���
#include <chrono>
#include <csignal> // ��ȣ ó���� ���� ���
#include <iostream>
#include <mutex> // ���ؽ� ����� ���� ���
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
#define RANDOM_STRING_LENGTH 32 // ������ ���ڿ� ����
#define NUM_CLIENTS 10000 // ������ Ŭ���̾�Ʈ ��

// ���� �ڿ�
std::mutex console_mutex;
std::atomic<bool> is_running(true); // ���α׷� ���� ���¸� �����ϴ� atomic ����

// Ctrl+C ��ȣ�� ó���ϱ� ���� �ڵ鷯
void signal_handler(int signal)
{
    if (signal == SIGINT)
    {
        std::cout << "\nCtrl+C detected. Shutting down clients..." << std::endl;
        is_running = false;
    }
}

// ������ ���ڿ��� �����ϴ� �Լ�
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

// �� Ŭ���̾�Ʈ�� ������ �����ϴ� �Լ�
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

    // is_running �÷��װ� true�� ���� ���� �ݺ�
    while (is_running)
    {
        std::string send_buffer = generate_random_string(RANDOM_STRING_LENGTH);

        if (send(client_socket, send_buffer.c_str(), (int)send_buffer.length(), 0) == SOCKET_ERROR)
        {
            if (is_running)
            { // ���� ���ᰡ �ƴ� ��쿡�� ���� �޽��� ���
                std::lock_guard<std::mutex> lock(console_mutex);
                std::cerr << "[Client " << client_id << "] Send failed: " << WSAGetLastError() << std::endl;
            }
            break;
        }

        int recvBytes = recv(client_socket, recv_buffer, BUFFER_SIZE - 1, 0);
        if (recvBytes <= 0)
        {
            if (is_running)
            { // ���� ���ᰡ �ƴ� ���
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

        // 0.1�� ���
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
    // ���� �ʱ�ȭ
    WSADATA wsa_data;
    if (WSAStartup(MAKEWORD(2, 2), &wsa_data) != 0)
    {
        std::cerr << "WSAStartup failed: " << WSAGetLastError() << std::endl;
        return 1;
    }

    // Ctrl+C ��ȣ ó���� ���
    signal(SIGINT, signal_handler);

    std::cout << "Creating " << NUM_CLIENTS << " clients for stress test..." << std::endl;
    std::cout << "Press Ctrl+C to stop the test." << std::endl;

    std::vector<std::thread> client_threads;
    for (int i = 0; i < NUM_CLIENTS; ++i)
    {
        client_threads.emplace_back(client_task, i + 1);
        std::this_thread::sleep_for(std::chrono::milliseconds(10)); // ���� ���� ���ϸ� ���̱� ���� �ణ�� ������
    }

    for (auto &th : client_threads)
    {
        if (th.joinable())
        {
            th.join();
        }
    }

    // ���� ����
    WSACleanup();

    std::cout << "Stress test finished." << std::endl;

    return 0;
}