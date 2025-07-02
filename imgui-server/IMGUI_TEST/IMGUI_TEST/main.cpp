#include "define.h"

// main.cpp
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "dxguid.lib")

#include <windows.h>
#include <d3d11.h>
#include <iostream>
#include <vector>
#include <string>

// Dear ImGui �����
#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

// DirectX11 ���� ������
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;

// �Լ� ����
bool CreateDeviceD3D(HWND hWnd);
void CleanupDeviceD3D();
void CreateRenderTarget();
void CleanupRenderTarget();
LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

// ���Ӽ��� ������ ����ü
struct Player {
    int id;
    std::string name;
    float x, y;
    int level;
    bool online;
};

struct Monster {
    int id;
    float x, y;
    int hp;
    int max_hp;
};

// ���� �Լ�
int main()
{
    // ������ Ŭ���� ���
    WNDCLASSEXW wc = {
        sizeof(wc), CS_CLASSDC, WndProc, 0L, 0L,
        GetModuleHandle(nullptr), nullptr, nullptr, nullptr, nullptr,
        L"GameServerDebug", nullptr
    };
    ::RegisterClassExW(&wc);

    // ������ ����
    HWND hwnd = ::CreateWindowW(wc.lpszClassName, L"Game Server Debug Tool - Win32+DX11",
        WS_OVERLAPPEDWINDOW, 100, 100, 1400, 900,
        nullptr, nullptr, wc.hInstance, nullptr);

    // DirectX11 �ʱ�ȭ
    if (!CreateDeviceD3D(hwnd))
    {
        CleanupDeviceD3D();
        ::UnregisterClassW(wc.lpszClassName, wc.hInstance);
        return 1;
    }

    // ������ ǥ��
    ::ShowWindow(hwnd, SW_SHOWDEFAULT);
    ::UpdateWindow(hwnd);

    // Dear ImGui �ʱ�ȭ
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    // Dear ImGui ��Ÿ��
    ImGui::StyleColorsDark();

    // �鿣�� �ʱ�ȭ
    ImGui_ImplWin32_Init(hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // ���Ӽ��� �ùķ��̼� ������ �ʱ�ȭ
    std::vector<Player> players = {
        {1, "Alice", 100.0f, 150.0f, 25, true},
        {2, "Bob", 200.0f, 250.0f, 30, true},
        {3, "Charlie", 300.0f, 180.0f, 15, true},
        {4, "Diana", 150.0f, 300.0f, 45, false}
    };

    std::vector<Monster> monsters = {
        {101, 150.0f, 100.0f, 80, 100},
        {102, 250.0f, 200.0f, 60, 80},
        {103, 350.0f, 300.0f, 100, 120},
        {104, 180.0f, 280.0f, 45, 90}
    };

    // ���� ���� ������
    int total_players_online = 3;
    int total_monsters = 4;
    float server_tps = 60.0f;
    float cpu_usage = 35.5f;
    float memory_usage = 512.3f;
    bool server_running = true;
    bool show_demo = false;

    // ���� ����
    bool done = false;
    while (!done)
    {
        // ������ �޽��� ó��
        MSG msg;
        while (::PeekMessage(&msg, nullptr, 0U, 0U, PM_REMOVE))
        {
            ::TranslateMessage(&msg);
            ::DispatchMessage(&msg);
            if (msg.message == WM_QUIT)
                done = true;
        }
        if (done)
            break;

        // �ùķ��̼� ������ ������Ʈ (�ǽð� ȿ��)
        static float time = 0.0f;
        time += 0.016f; // ~60 FPS

        // �÷��̾� ��ġ �ùķ��̼�
        for (auto& player : players) {
            if (player.online) {
                player.x = 200.0f + sin(time + player.id) * 100.0f;
                player.y = 200.0f + cos(time + player.id) * 80.0f;
            }
        }

        // ���� ������ �� HP �ùķ��̼�
        for (auto& monster : monsters) {
            monster.x = 300.0f + cos(time * 0.5f + monster.id) * 120.0f;
            monster.y = 250.0f + sin(time * 0.5f + monster.id) * 100.0f;
            // HP ���� ��ȭ (���� �ùķ��̼�)
            if (rand() % 100 < 2) { // 2% Ȯ���� HP ��ȭ
                monster.hp = max(0, monster.hp - (rand() % 10 + 1));
            }
        }

        // ���� ��Ʈ�� �ùķ��̼�
        cpu_usage = 30.0f + sin(time * 0.3f) * 15.0f;
        memory_usage = 500.0f + cos(time * 0.2f) * 50.0f;

        // Dear ImGui ������ ����
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // 1. ���� ���� ���� ������
        {
            ImGui::Begin("Server Status Dashboard");

            ImGui::Text("Game Server Management Console");
            ImGui::Separator();

            // ���� ���� ǥ��
            ImGui::Text("Status: ");
            ImGui::SameLine();
            if (server_running) {
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "ONLINE");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(0.0f, 1.0f, 0.0f, 1.0f), "��");
            }
            else {
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "OFFLINE");
                ImGui::SameLine();
                ImGui::TextColored(ImVec4(1.0f, 0.0f, 0.0f, 1.0f), "��");
            }

            // �ǽð� ���
            ImGui::Separator();
            ImGui::Text("Real-time Statistics:");
            ImGui::Text("  Players Online: %d", total_players_online);
            ImGui::Text("  Monsters Active: %d", total_monsters);
            ImGui::Text("  Server TPS: %.1f", server_tps);
            ImGui::Text("  CPU Usage: %.1f%%", cpu_usage);
            ImGui::Text("  Memory Usage: %.1f MB", memory_usage);

            // ���α׷��� ��
            ImGui::Separator();
            ImGui::Text("System Resources:");
            ImGui::ProgressBar(cpu_usage / 100.0f, ImVec2(-1, 0), "CPU");
            ImGui::ProgressBar(memory_usage / 1024.0f, ImVec2(-1, 0), "Memory");

            // ���� ���� ��ư��
            ImGui::Separator();
            ImGui::Text("Server Controls:");

            if (ImGui::Button("Restart Server")) {
                std::cout << "Server Restarting..." << std::endl;
                server_running = false;
                // �����δ� ���� ����� ����
            }
            ImGui::SameLine();

            if (ImGui::Button("Save State")) {
                std::cout << "Server State Saved!" << std::endl;
            }
            ImGui::SameLine();

            if (ImGui::Button("Load State")) {
                std::cout << "Server State Loaded!" << std::endl;
            }

            if (ImGui::Button("Emergency Stop")) {
                std::cout << "Emergency Stop Initiated!" << std::endl;
                server_running = false;
            }
            ImGui::SameLine();

            if (ImGui::Button("Start Server")) {
                std::cout << "Server Starting..." << std::endl;
                server_running = true;
            }

            // ���� �����̴���
            ImGui::Separator();
            ImGui::Text("Server Configuration:");
            ImGui::SliderFloat("Target TPS", &server_tps, 30.0f, 120.0f);
            ImGui::SliderInt("Max Players", &total_players_online, 0, 1000);

            ImGui::Checkbox("Show ImGui Demo", &show_demo);

            ImGui::End();
        }

        // 2. ���� �� �ð�ȭ ������
        {
            ImGui::Begin("World Map Visualization");

            // ĵ���� ���� ����
            ImDrawList* draw_list = ImGui::GetWindowDrawList();
            ImVec2 canvas_pos = ImGui::GetCursorScreenPos();
            ImVec2 canvas_size = ImVec2(600, 400);

            // ĵ���� ���
            draw_list->AddRectFilled(canvas_pos,
                ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
                IM_COL32(40, 40, 50, 255));

            // ĵ���� �׵θ�
            draw_list->AddRect(canvas_pos,
                ImVec2(canvas_pos.x + canvas_size.x, canvas_pos.y + canvas_size.y),
                IM_COL32(100, 100, 100, 255));

            // ���� �׸���
            for (int i = 0; i <= 10; i++) {
                float x = canvas_pos.x + (canvas_size.x / 10) * i;
                float y = canvas_pos.y + (canvas_size.y / 10) * i;

                draw_list->AddLine(ImVec2(x, canvas_pos.y),
                    ImVec2(x, canvas_pos.y + canvas_size.y),
                    IM_COL32(60, 60, 60, 128));

                draw_list->AddLine(ImVec2(canvas_pos.x, y),
                    ImVec2(canvas_pos.x + canvas_size.x, y),
                    IM_COL32(60, 60, 60, 128));
            }

            // �÷��̾�� �׸��� (�ʷ� ��)
            for (const auto& player : players) {
                if (player.online) {
                    ImVec2 pos(canvas_pos.x + player.x * 0.8f, canvas_pos.y + player.y * 0.8f);
                    draw_list->AddCircleFilled(pos, 8.0f, IM_COL32(0, 255, 0, 255));

                    // �÷��̾� �̸� ǥ��
                    draw_list->AddText(ImVec2(pos.x + 12, pos.y - 8),
                        IM_COL32(255, 255, 255, 255), player.name.c_str());
                }
            }

            // ���͵� �׸��� (���� ��)
            for (const auto& monster : monsters) {
                ImVec2 pos(canvas_pos.x + monster.x * 0.8f, canvas_pos.y + monster.y * 0.8f);
                draw_list->AddCircleFilled(pos, 6.0f, IM_COL32(255, 0, 0, 255));

                // HP �� �׸���
                float hp_ratio = (float)monster.hp / monster.max_hp;
                ImVec2 hp_start(pos.x - 15, pos.y - 15);
                ImVec2 hp_end(pos.x + 15, pos.y - 12);

                // HP ���
                draw_list->AddRectFilled(hp_start, hp_end, IM_COL32(50, 50, 50, 200));
                // HP ��
                draw_list->AddRectFilled(hp_start,
                    ImVec2(hp_start.x + (hp_end.x - hp_start.x) * hp_ratio, hp_end.y),
                    IM_COL32(255, 100, 100, 255));
            }

            // ����
            ImGui::SetCursorScreenPos(ImVec2(canvas_pos.x, canvas_pos.y + canvas_size.y + 10));
            ImGui::Text("Legend:");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(0, 1, 0, 1), "�� Players");
            ImGui::SameLine();
            ImGui::TextColored(ImVec4(1, 0, 0, 1), "�� Monsters");

            // ĵ���� ũ�⸸ŭ ���� ����
            ImGui::SetCursorScreenPos(ImVec2(canvas_pos.x, canvas_pos.y + canvas_size.y + 30));

            ImGui::End();
        }

        // 3. �÷��̾� ��� ������
        {
            ImGui::Begin("Player Management");

            ImGui::Text("Connected Players:");
            ImGui::Separator();

            if (ImGui::BeginTable("PlayerTable", 6,
                ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable)) {

                ImGui::TableSetupColumn("ID", ImGuiTableColumnFlags_WidthFixed, 40);
                ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthFixed, 80);
                ImGui::TableSetupColumn("Position", ImGuiTableColumnFlags_WidthFixed, 100);
                ImGui::TableSetupColumn("Level", ImGuiTableColumnFlags_WidthFixed, 50);
                ImGui::TableSetupColumn("Status", ImGuiTableColumnFlags_WidthFixed, 60);
                ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthStretch);
                ImGui::TableHeadersRow();

                for (const auto& player : players) {
                    ImGui::TableNextRow();

                    ImGui::TableNextColumn();
                    ImGui::Text("%d", player.id);

                    ImGui::TableNextColumn();
                    ImGui::Text("%s", player.name.c_str());

                    ImGui::TableNextColumn();
                    ImGui::Text("(%.0f, %.0f)", player.x, player.y);

                    ImGui::TableNextColumn();
                    ImGui::Text("%d", player.level);

                    ImGui::TableNextColumn();
                    if (player.online) {
                        ImGui::TextColored(ImVec4(0, 1, 0, 1), "Online");
                    }
                    else {
                        ImGui::TextColored(ImVec4(0.5f, 0.5f, 0.5f, 1), "Offline");
                    }

                    ImGui::TableNextColumn();
                    ImGui::PushID(player.id);
                    if (ImGui::SmallButton("Kick")) {
                        std::cout << "Kicked player " << player.name << std::endl;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Message")) {
                        std::cout << "Sent message to " << player.name << std::endl;
                    }
                    ImGui::SameLine();
                    if (ImGui::SmallButton("Teleport")) {
                        std::cout << "Teleported " << player.name << std::endl;
                    }
                    ImGui::PopID();
                }

                ImGui::EndTable();
            }

            ImGui::End();
        }

        // 4. ���� ���� ������
        {
            ImGui::Begin("Monster Status");

            ImGui::Text("Active Monsters:");
            ImGui::Separator();

            for (const auto& monster : monsters) {
                ImGui::Text("Monster ID %d", monster.id);
                ImGui::SameLine();
                ImGui::Text("HP: %d/%d", monster.hp, monster.max_hp);

                // HP ��
                float hp_ratio = (float)monster.hp / monster.max_hp;
                ImGui::ProgressBar(hp_ratio, ImVec2(-1, 0));

                if (monster.hp == 0) {
                    ImGui::SameLine();
                    ImGui::TextColored(ImVec4(1, 0, 0, 1), "DEAD");
                }

                ImGui::Separator();
            }

            if (ImGui::Button("Respawn All Monsters")) {
                for (auto& monster : monsters) {
                    monster.hp = monster.max_hp;
                }
                std::cout << "All monsters respawned!" << std::endl;
            }

            ImGui::End();
        }

        // 5. ImGui ���� ������ (�ɼ�)
        if (show_demo) {
            ImGui::ShowDemoWindow(&show_demo);
        }

        // ������
        ImGui::Render();
        const float clear_color[4] = { 0.45f, 0.55f, 0.60f, 1.00f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clear_color);
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        g_pSwapChain->Present(1, 0); // VSync
    }

    // ����
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();

    CleanupDeviceD3D();
    ::DestroyWindow(hwnd);
    ::UnregisterClassW(wc.lpszClassName, wc.hInstance);

    return 0;
}

// DirectX11 ���� �Լ���
IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

LRESULT WINAPI WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;

    switch (msg)
    {
    case WM_SIZE:
        if (g_pd3dDevice != nullptr && wParam != SIZE_MINIMIZED)
        {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_SYSCOMMAND:
        if ((wParam & 0xfff0) == SC_KEYMENU)
            return 0;
        break;
    case WM_DESTROY:
        ::PostQuitMessage(0);
        return 0;
    }
    return ::DefWindowProcW(hWnd, msg, wParam, lParam);
}

bool CreateDeviceD3D(HWND hWnd)
{
    DXGI_SWAP_CHAIN_DESC sd;
    ZeroMemory(&sd, sizeof(sd));
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    UINT createDeviceFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    const D3D_FEATURE_LEVEL featureLevelArray[2] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_0, };
    HRESULT res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res == DXGI_ERROR_UNSUPPORTED)
        res = D3D11CreateDeviceAndSwapChain(nullptr, D3D_DRIVER_TYPE_WARP, nullptr, createDeviceFlags, featureLevelArray, 2, D3D11_SDK_VERSION, &sd, &g_pSwapChain, &g_pd3dDevice, &featureLevel, &g_pd3dDeviceContext);
    if (res != S_OK)
        return false;

    CreateRenderTarget();
    return true;
}

void CleanupDeviceD3D()
{
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

void CreateRenderTarget()
{
    ID3D11Texture2D* pBackBuffer;
    g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&pBackBuffer));
    g_pd3dDevice->CreateRenderTargetView(pBackBuffer, nullptr, &g_mainRenderTargetView);
    pBackBuffer->Release();
}

void CleanupRenderTarget()
{
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

