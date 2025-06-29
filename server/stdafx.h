#pragma once

// =================================================
// 중요: winsock2.h는 windows.h 보다 반드시 먼저 와야함
#include <winsock2.h>
#include <mswsock.h>
#include <windows.h> // winsock2.h 보다 뒤에 위치
// =================================================

#include <iostream>
#include <vector>
#include <thread>


#pragma comment(lib, "ws2_32.lib")