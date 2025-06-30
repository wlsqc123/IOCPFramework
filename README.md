# IOCPFramework

## 아키텍처 구조도

```mermaid
graph TD
    subgraph "서버 측"
        A[IocpServer]
        A -- " " --> B(리슨 소켓)
        A -- " " --> C{IOCP 핸들}
        A -- " " --> D[Accept 스레드]
        A -- " " --> E[워커 스레드]

        D -- "accept()" --> F(클라이언트 소켓)
        D -- "CreateIoCompletionPort()" --> C
        D -- "생성" --> G[ClientSession]

        subgraph "다중 클라이언트 세션"
            G
            H[ClientSession]
            I[...]
        end

        G -- "WSARecv(OVERLAPPED)" --> C
        H -- "WSARecv(OVERLAPPED)" --> C


        E -- "GetQueuedCompletionStatus()" --> C
        E -- "I/O 처리" --> G
        E -- "I/O 처리" --> H
    end

    subgraph "클라이언트 측"
        J[클라이언트 애플리케이션]
        J -- "socket() & connect()" --> B
        J -- "send()" --> F
        F -- "WSASend()" --> J
    end

    style A fill:#f9f,stroke:#333,stroke-width:2px
    style J fill:#ccf,stroke:#333,stroke-width:2px