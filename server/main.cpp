#include "../common/Types.h"
#include <cstdio>
#include <iostream>

using namespace IOCPFramework;

int main() {
    printf("========================================\n");
    printf("IOCPFramework v0.1.0\n");
    printf("========================================\n");
#ifdef _DEBUG
    printf("Build Type: Debug\n");
#else
    printf("Build Type: Release\n");
#endif
    printf("C++ Standard: C++%ld\n", __cplusplus / 100);
    printf("========================================\n\n");

    printf("Hello, IOCP Framework!\n");
    printf("Press Enter to exit...\n");

    std::cin.get();
    
    return 0;
}
