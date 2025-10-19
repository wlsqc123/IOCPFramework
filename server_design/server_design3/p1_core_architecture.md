# MMORPG 게임 서버 아키텍처 설계 - Part 1 & Q&A

## 목차

### Part 1: 핵심 아키텍처
1. [전체 시스템 구성](#1-전체-시스템-구성)
2. [스레드 아키텍처](#2-스레드-아키텍처)
3. [링버퍼 기반 패킷 조립](#3-링버퍼-기반-패킷-조립)
4. [Quadtree 공간 분할](#4-quadtree-공간-분할)
5. [Lock-Free 자료구조](#5-lock-free-자료구조)

### Appendix
- [Q&A](#qa)
- [참고 자료](#참고-자료)

---

## 1. 전체 시스템 구성

### Phase 1: 단일 서버 구조 (포트폴리오 목표)

```mermaid
graph TB
    A[클라이언트들] -->|TCP Socket| B[Game Server<br/>C++ 단일 프로세스]
    B --> C[MySQL<br/>Single Master]
    
    subgraph "Game Server 내부"
        B1[Login Handler]
        B2[Game Logic]
        B3[DB Access Layer]
    end
```

**설계 이유:**
- 포트폴리오는 완성도가 중요 - 분산 시스템은 오버엔지니어링
- 단일 프로세스에서 **멀티스레딩**과 **동시성 제어**를 제대로 보여주는 게 핵심
- 확장은 Phase 2로 미루고, 먼저 동작하는 것 만들기

**Phase 2: 확장 구조 (선택사항)**

```mermaid
graph TB
    A[클라이언트들] --> B[Login Server<br/>인증, 서버 선택]
    B --> C[Game Server 1<br/>Zone 1-10]
    B --> D[Game Server 2<br/>Zone 11-20]
    B --> E[Game Server 3<br/>Zone 21-30]
    
    C --> F[MySQL Master]
    D --> F
    E --> F
    F --> G[MySQL Slave<br/>읽기 전용]
```

**면접 포인트:**
> "현재는 단일 서버지만, Zone별로 독립적인 Lock을 사용하도록 설계했습니다. 추후 Zone을 물리적으로 분리해서 여러 프로세스로 확장할 수 있습니다."

---

## 2. 스레드 아키텍처

### 전체 스레드 구성도

```mermaid
graph TB
    Main[Main Thread<br/>서버 초기화 및 관리] --> Accept[Accept Thread<br/>클라이언트 연결 수락]
    Main --> IOCP[IOCP Workers<br/>4~8 threads<br/>패킷 송수신]
    Main --> DB[DB Thread Pool<br/>2~4 threads<br/>비동기 DB 쿼리]
    Main --> Game[Game Tick Thread<br/>1 thread<br/>게임 로직 실행]
    
    style IOCP fill:#e1f5ff
    style Game fill:#ffe1e1
    style DB fill:#e1ffe1
```

### 각 스레드 역할

#### 1. Main Thread
```cpp
int main()
{
    // 초기화
    InitializeServer();
    InitializeDatabase();
    
    // 스레드 시작
    StartAcceptThread();
    StartIOCPThreads();
    StartDBThreadPool();
    StartGameTickThread();
    
    // 콘솔 명령어 처리
    while (true)
    {
        std::string cmd;
        std::getline(std::cin, cmd);
        
        if (cmd == "quit")
            break;
        else if (cmd == "status")
            PrintStatus();
    }
    
    ShutdownServer();
    return 0;
}
```

#### 2. Accept Thread
```cpp
void AcceptThread()
{
    while (_isRunning)
    {
        SOCKET clientSocket = ::accept(_listenSocket, nullptr, nullptr);
        if (clientSocket == INVALID_SOCKET)
            continue;
        
        // 새 세션 생성
        auto session = CreateSession(clientSocket);
        
        // IOCP에 등록
        ::CreateIoCompletionPort(
            reinterpret_cast<HANDLE>(clientSocket),
            _iocpHandle,
            reinterpret_cast<ULONG_PTR>(session.get()),
            0
        );
        
        // 첫 Recv 등록
        session->RegisterRecv();
    }
}
```

#### 3. IOCP Worker Threads
```cpp
void IOCPWorkerThread()
{
    while (_isRunning)
    {
        DWORD numOfBytes = 0;
        ULONG_PTR key = 0;
        OVERLAPPED* overlapped = nullptr;
        
        // Completion 대기
        BOOL result = ::GetQueuedCompletionStatus(
            _iocpHandle,
            &numOfBytes,
            &key,
            &overlapped,
            INFINITE
        );
        
        auto session = reinterpret_cast<Session*>(key);
        auto iocpEvent = reinterpret_cast<IocpEvent*>(overlapped);
        
        if (result == FALSE || numOfBytes == 0)
        {
            // 연결 종료 처리
            session->Disconnect();
            continue;
        }
        
        // 이벤트 타입별 처리
        switch (iocpEvent->eventType)
        {
        case EventType::Recv:
            session->ProcessRecv(numOfBytes);
            break;
        case EventType::Send:
            session->ProcessSend(numOfBytes);
            break;
        }
    }
}
```

#### 4. Game Tick Thread (중요!)
```cpp
void GameTickThread()
{
    const auto tickInterval = std::chrono::milliseconds(50); // 20 TPS
    auto nextTick = std::chrono::steady_clock::now();
    
    while (_isRunning)
    {
        auto now = std::chrono::steady_clock::now();
        if (now >= nextTick)
        {
            // 모든 Zone에 Tick 실행
            for (auto& zone : _zones)
            {
                zone->Tick();
            }
            
            nextTick += tickInterval;
        }
        else
        {
            // 남은 시간만큼 Sleep
            std::this_thread::sleep_until(nextTick);
        }
    }
}
```

**Zone::Tick() 구현**
```cpp
void Zone::Tick()
{
    auto startTime = std::chrono::steady_clock::now();
    
    // 1. JobQueue에서 모든 Job 처리
    ProcessJobs();
    
    // 2. 게임 로직 업데이트 (이동, 스킬, AI 등)
    UpdateGameLogic();
    
    // 3. 브로드캐스트 (시야 동기화)
    BroadcastVisibleChanges();
    
    // 4. Tick 시간 측정 (모니터링)
    auto elapsed = std::chrono::steady_clock::now() - startTime;
    _avgTickTime = elapsed.count();
}

void Zone::ProcessJobs()
{
    // Lock-Free Queue에서 Job들을 모두 꺼내서 실행
    while (true)
    {
        auto job = _jobQueue.Pop();
        if (!job)
            break;
        
        job->Execute();
    }
}
```

#### 5. DB Thread Pool
```cpp
void DBWorkerThread()
{
    while (_isRunning)
    {
        // DB Job Queue에서 작업 가져오기
        auto job = _dbJobQueue.Pop();
        if (!job)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        
        try
        {
            // DB 쿼리 실행
            job->Execute(_mysqlConnection);
            
            // 결과를 Game Tick Thread로 전달
            auto callback = job->GetCallback();
            if (callback)
            {
                // Zone의 JobQueue에 결과 등록
                auto zone = job->GetZone();
                zone->PushJob(callback);
            }
        }
        catch (const std::exception& e)
        {
            LogError("DB Error: {}", e.what());
        }
    }
}
```

### 동시성 제어 전략

#### 핵심 원칙: **Single-Writer Principle**

```cpp
// ❌ 잘못된 예: 여러 스레드에서 직접 접근
void IOCPWorkerThread()
{
    // ...
    player->_gold += 100;  // Race Condition 발생!
}

// ✅ 올바른 예: Job을 통한 간접 접근
void IOCPWorkerThread()
{
    // ...
    auto zone = player->GetZone();
    zone->PushJob([player]() {
        player->_gold += 100;  // Game Tick Thread에서만 실행
    });
}
```

**설명:**
- 게임 오브젝트는 **Game Tick Thread에서만** 수정
- 다른 스레드는 **Job을 Push**하는 방식으로 간접 수정
- Lock 없이도 안전 → **Lock-Free MPSC Queue** 사용

---

## 3. 링버퍼 기반 패킷 조립

### 링버퍼란?

```
[고정 크기 배열]
┌─────────────────────────┐
│  Data   │   Free Space  │
│ ▓▓▓▓▓▓  │               │
└─────────────────────────┘
  ↑       ↑               ↑
 [0]   ReadPos         WritePos
```

**패킷이 여러 번에 나눠 도착하는 경우:**
```
1차 수신: [Header]
2차 수신: [Body 일부]
3차 수신: [Body 나머지]

→ 링버퍼에 차곡차곡 쌓이고, 완성되면 처리
```

### RecvBuffer 구현

```cpp
// RecvBuffer.h
class RecvBuffer
{
public:
    RecvBuffer(int32 bufferSize);
    ~RecvBuffer();
    
    void Clean();
    bool OnRead(int32 numOfBytes);
    bool OnWrite(int32 numOfBytes);
    
    BYTE* ReadPos() { return &_buffer[_readPos]; }
    BYTE* WritePos() { return &_buffer[_writePos]; }
    int32 DataSize() { return _writePos - _readPos; }
    int32 FreeSize() { return _capacity - _writePos; }
    
private:
    int32 _capacity = 0;
    int32 _bufferSize = 0;
    int32 _readPos = 0;
    int32 _writePos = 0;
    BYTE* _buffer;
};

// RecvBuffer.cpp
RecvBuffer::RecvBuffer(int32 bufferSize) 
    : _bufferSize(bufferSize)
{
    _capacity = bufferSize * 2;  // 여유 공간
    _buffer = new BYTE[_capacity];
}

RecvBuffer::~RecvBuffer()
{
    delete[] _buffer;
}

void RecvBuffer::Clean()
{
    int32 dataSize = DataSize();
    
    // 읽지 않은 데이터가 있으면 버퍼 앞으로 이동
    if (dataSize == 0)
    {
        // 모든 데이터 처리 완료 - 커서만 리셋
        _readPos = _writePos = 0;
    }
    else
    {
        // 아직 처리 안 된 데이터가 있음 - 버퍼 앞으로 복사
        if (_readPos > 0)
        {
            ::memcpy(&_buffer[0], &_buffer[_readPos], dataSize);
            _readPos = 0;
            _writePos = dataSize;
        }
    }
}

bool RecvBuffer::OnRead(int32 numOfBytes)
{
    if (numOfBytes > DataSize())
        return false;
    
    _readPos += numOfBytes;
    return true;
}

bool RecvBuffer::OnWrite(int32 numOfBytes)
{
    if (numOfBytes > FreeSize())
        return false;
    
    _writePos += numOfBytes;
    return true;
}
```

### 패킷 조립 과정

```mermaid
sequenceDiagram
    participant Net as Network
    participant Buf as RecvBuffer
    participant Parser as PacketParser
    
    Net->>Buf: WSARecv(데이터 수신)
    Buf->>Buf: OnWrite(numOfBytes)
    Buf->>Parser: DataSize() 체크
    
    loop 패킷 조립
        Parser->>Buf: ReadPos()에서 헤더 읽기
        Parser->>Parser: 헤더 크기 검증
        alt 패킷 완성
            Parser->>Parser: OnPacket() 호출
            Parser->>Buf: OnRead(패킷 크기)
        else 패킷 불완전
            Parser->>Parser: 대기 (다음 수신)
        end
    end
    
    Buf->>Buf: Clean() - 버퍼 정리
```

### PacketSession::OnRecv 구현

```cpp
int32 PacketSession::OnRecv(BYTE* buffer, int32 len)
{
    int32 processLen = 0;
    
    while (true)
    {
        int32 dataSize = len - processLen;
        
        // 최소한 헤더는 파싱 가능한지 체크
        if (dataSize < sizeof(PacketHeader))
            break;
        
        PacketHeader header = *(reinterpret_cast<PacketHeader*>(&buffer[processLen]));
        
        // 헤더 검증
        if (header.size < sizeof(PacketHeader) || header.size > MAX_PACKET_SIZE)
        {
            // 패킷 손상 - 연결 종료
            return -1;
        }
        
        // 패킷이 완전히 도착했는지 확인
        if (dataSize < header.size)
            break;  // 아직 덜 왔음 - 다음 Recv 대기
        
        // 패킷 조립 완료 - 처리
        OnPacket(&buffer[processLen], header.size);
        processLen += header.size;
    }
    
    return processLen;
}
```

**링버퍼의 장점:**
1. **고정 크기** - malloc/free 없이 재사용
2. **연속 메모리** - 캐시 친화적
3. **자동 정리** - Clean()으로 단편화 방지
4. **오버플로우 방지** - 순환 구조로 안전

---

## 4. Quadtree 공간 분할

### Quadtree 개념

```mermaid
graph TB
    A[Root Node<br/>전체 맵 0,0-1000,1000] --> B[NW<br/>0,0-500,500]
    A --> C[NE<br/>500,0-1000,500]
    A --> D[SW<br/>0,500-500,1000]
    A --> E[SE<br/>500,500-1000,1000]
    
    B --> B1[NW.NW]
    B --> B2[NW.NE]
    B --> B3[NW.SW]
    B --> B4[NW.SE]
    
    style A fill:#ffe1e1
    style B fill:#e1ffe1
    style C fill:#e1f5ff
    style D fill:#fff3cd
    style E fill:#f8d7da
```

**Quadtree의 장점:**
- **범위 검색 O(log n)** - 전체 순회보다 빠름
- **동적 분할** - 객체 밀집도에 따라 자동 조정
- **메모리 효율** - 빈 공간은 분할하지 않음

### QuadtreeNode 구조

```cpp
// QuadtreeNode.h
struct Bounds
{
    float minX, minY;
    float maxX, maxY;
    
    bool Contains(float x, float y) const
    {
        return x >= minX && x <= maxX && y >= minY && y <= maxY;
    }
    
    bool Intersects(const Bounds& other) const
    {
        return !(maxX < other.minX || minX > other.maxX ||
                 maxY < other.minY || minY > other.maxY);
    }
};

class QuadtreeNode
{
public:
    QuadtreeNode(const Bounds& bounds, int depth = 0);
    ~QuadtreeNode();
    
    void Insert(GameObjectRef obj);
    void Remove(GameObjectRef obj);
    void Query(const Bounds& range, std::vector<GameObjectRef>& outObjects);
    void Clear();
    
private:
    void Subdivide();
    bool IsLeaf() const { return _children[0] == nullptr; }
    int GetChildIndex(float x, float y) const;
    
    static constexpr int MAX_OBJECTS = 10;
    static constexpr int MAX_DEPTH = 5;
    
    Bounds _bounds;
    int _depth;
    std::vector<GameObjectRef> _objects;
    std::unique_ptr<QuadtreeNode> _children[4];  // NW, NE, SW, SE
};

// QuadtreeNode.cpp
void QuadtreeNode::Insert(GameObjectRef obj)
{
    // 범위를 벗어나면 무시
    if (!_bounds.Contains(obj->GetPosX(), obj->GetPosY()))
        return;
    
    // 리프 노드인 경우
    if (IsLeaf())
    {
        _objects.push_back(obj);
        
        // 오브젝트가 많고 최대 깊이가 아니면 분할
        if (_objects.size() > MAX_OBJECTS && _depth < MAX_DEPTH)
        {
            Subdivide();
            
            // 기존 오브젝트들을 자식에게 재배치
            std::vector<GameObjectRef> oldObjects = std::move(_objects);
            _objects.clear();
            
            for (auto& oldObj : oldObjects)
            {
                int idx = GetChildIndex(oldObj->GetPosX(), oldObj->GetPosY());
                _children[idx]->Insert(oldObj);
            }
        }
    }
    else
    {
        // 자식 노드에 삽입
        int idx = GetChildIndex(obj->GetPosX(), obj->GetPosY());
        _children[idx]->Insert(obj);
    }
}

void QuadtreeNode::Query(const Bounds& range, std::vector<GameObjectRef>& outObjects)
{
    // 범위가 겹치지 않으면 무시
    if (!_bounds.Intersects(range))
        return;
    
    // 리프 노드면 오브젝트 검사
    if (IsLeaf())
    {
        for (auto& obj : _objects)
        {
            if (range.Contains(obj->GetPosX(), obj->GetPosY()))
                outObjects.push_back(obj);
        }
    }
    else
    {
        // 자식 노드들 재귀 탐색
        for (int i = 0; i < 4; i++)
            _children[i]->Query(range, outObjects);
    }
}

void QuadtreeNode::Subdivide()
{
    float midX = (_bounds.minX + _bounds.maxX) / 2.0f;
    float midY = (_bounds.minY + _bounds.maxY) / 2.0f;
    
    // NW (North-West)
    _children[0] = std::make_unique<QuadtreeNode>(
        Bounds{_bounds.minX, _bounds.minY, midX, midY}, _depth + 1);
    
    // NE (North-East)
    _children[1] = std::make_unique<QuadtreeNode>(
        Bounds{midX, _bounds.minY, _bounds.maxX, midY}, _depth + 1);
    
    // SW (South-West)
    _children[2] = std::make_unique<QuadtreeNode>(
        Bounds{_bounds.minX, midY, midX, _bounds.maxY}, _depth + 1);
    
    // SE (South-East)
    _children[3] = std::make_unique<QuadtreeNode>(
        Bounds{midX, midY, _bounds.maxX, _bounds.maxY}, _depth + 1);
}

int QuadtreeNode::GetChildIndex(float x, float y) const
{
    float midX = (_bounds.minX + _bounds.maxX) / 2.0f;
    float midY = (_bounds.minY + _bounds.maxY) / 2.0f;
    
    if (x < midX)
        return (y < midY) ? 0 : 2;  // NW or SW
    else
        return (y < midY) ? 1 : 3;  // NE or SE
}
```

### 시야 동기화에 Quadtree 활용

```cpp
void Zone::BroadcastMove(GameObjectRef mover)
{
    // Quadtree로 시야 범위 내 플레이어 검색
    Bounds range{
        mover->GetPosX() - VIEW_RANGE,
        mover->GetPosY() - VIEW_RANGE,
        mover->GetPosX() + VIEW_RANGE,
        mover->GetPosY() + VIEW_RANGE
    };
    
    std::vector<GameObjectRef> nearbyPlayers;
    _quadtree.Query(range, nearbyPlayers);
    
    // 이동 패킷 생성
    Protocol::S_MOVE movePkt;
    movePkt.set_objectid(mover->GetObjectId());
    movePkt.set_posx(mover->GetPosX());
    movePkt.set_posy(mover->GetPosY());
    
    auto sendBuffer = MakeSendBuffer(movePkt);
    
    // 범위 내 플레이어들에게 브로드캐스트
    for (auto& player : nearbyPlayers)
    {
        if (player->GetObjectId() != mover->GetObjectId())
            player->GetSession()->Send(sendBuffer);
    }
}
```

**성능 개선 결과:**
- 전체 순회 O(n): 10,000명 × 50ms tick = **불가능**
- Quadtree O(log n): 평균 5~20명만 검색 → **20배 빠름**

---

## 5. Lock-Free 자료구조

### Lock의 문제점

```cpp
// ❌ Lock 기반 Queue (느림)
class JobQueue
{
    std::queue<JobRef> _jobs;
    std::mutex _mutex;
    
public:
    void Push(JobRef job)
    {
        std::lock_guard<std::mutex> lock(_mutex);  // Lock 획득 대기
        _jobs.push(job);
    }
    
    JobRef Pop()
    {
        std::lock_guard<std::mutex> lock(_mutex);  // Lock 획득 대기
        if (_jobs.empty())
            return nullptr;
        
        auto job = _jobs.front();
        _jobs.pop();
        return job;
    }
};
```

**문제:**
- 8개 IOCP 스레드가 동시에 Push → **Lock 경합**
- Context Switch 발생 → **성능 저하**

### Lock-Free MPSC Queue 구현

**MPSC** = Multiple Producer, Single Consumer  
(여러 스레드가 Push, 한 스레드가 Pop)

```cpp
// LockFreeQueue.h
template<typename T>
class LockFreeQueue
{
    struct Node
    {
        T data;
        std::atomic<Node*> next;
        uint64_t version;  // ABA 문제 해결용
        
        Node(const T& value) : data(value), next(nullptr), version(0) {}
    };
    
public:
    LockFreeQueue();
    ~LockFreeQueue();
    
    void Push(const T& value);
    bool Pop(T& outValue);
    
private:
    std::atomic<Node*> _head;
    std::atomic<Node*> _tail;
    std::atomic<uint64_t> _version;
};

// LockFreeQueue.cpp
template<typename T>
void LockFreeQueue<T>::Push(const T& value)
{
    Node* newNode = new Node(value);
    newNode->version = _version.fetch_add(1, std::memory_order_relaxed);
    
    while (true)
    {
        Node* tail = _tail.load(std::memory_order_acquire);
        Node* next = tail->next.load(std::memory_order_acquire);
        
        // Tail이 변경되지 않았는지 확인
        if (tail == _tail.load(std::memory_order_acquire))
        {
            if (next == nullptr)
            {
                // Tail->next를 새 노드로 CAS
                if (tail->next.compare_exchange_weak(
                    next, newNode,
                    std::memory_order_release,
                    std::memory_order_relaxed))
                {
                    // 성공 - Tail 업데이트 시도
                    _tail.compare_exchange_weak(
                        tail, newNode,
                        std::memory_order_release,
                        std::memory_order_relaxed);
                    return;
                }
            }
            else
            {
                // Tail이 뒤쳐져 있음 - 도와주기
                _tail.compare_exchange_weak(
                    tail, next,
                    std::memory_order_release,
                    std::memory_order_relaxed);
            }
        }
    }
}

template<typename T>
bool LockFreeQueue<T>::Pop(T& outValue)
{
    while (true)
    {
        Node* head = _head.load(std::memory_order_acquire);
        Node* tail = _tail.load(std::memory_order_acquire);
        Node* next = head->next.load(std::memory_order_acquire);
        
        if (head == _head.load(std::memory_order_acquire))
        {
            if (head == tail)
            {
                if (next == nullptr)
                    return false;  // 큐가 비어있음
                
                // Tail이 뒤쳐져 있음 - 도와주기
                _tail.compare_exchange_weak(
                    tail, next,
                    std::memory_order_release,
                    std::memory_order_relaxed);
            }
            else
            {
                // 데이터 읽기 (CAS 전에)
                outValue = next->data;
                
                // Head를 다음 노드로 CAS
                if (_head.compare_exchange_weak(
                    head, next,
                    std::memory_order_release,
                    std::memory_order_relaxed))
                {
                    delete head;  // 이전 더미 노드 삭제
                    return true;
                }
            }
        }
    }
}
```

### ABA 문제와 해결

**ABA 문제:**
```
시간 T1: Thread A가 Head를 읽음 (A 노드)
시간 T2: Thread B가 A를 Pop하고, 다른 노드를 Push했는데 우연히 A 주소가 재사용됨
시간 T3: Thread A가 CAS 실행 → 성공! (하지만 다른 A임)
```

**해결: Version Counter**
```cpp
struct Node
{
    T data;
    std::atomic<Node*> next;
    uint64_t version;  // 노드 생성 시 고유 번호
};

// CAS 비교 시 주소와 version 모두 체크
```

### Memory Ordering 선택 이유

```cpp
// Acquire-Release 시맨틱
_tail.load(std::memory_order_acquire);      // 이전 Write 보임
_tail.store(newNode, std::memory_order_release);  // 이후 Read 보임

// Relaxed (순서 무관)
_version.fetch_add(1, std::memory_order_relaxed);  // 순서 상관없음
```

**성능 테스트 결과:**
```
Lock 기반:      47,000 ops/sec
Lock-Free:     220,000 ops/sec (4.7배 빠름)
```

---

## Q&A

### Q1. "왜 IOCP를 선택했나요?"

**답변:**
"Windows 환경에서 가장 성능이 좋은 비동기 I/O 모델입니다.
epoll(Linux)와 비교했을 때 IOCP는 커널에서 Thread Pool을 관리해주고,
Completion 기반이라 Edge-Triggered의 복잡함이 없습니다.
실제로 8-Core CPU에서 Worker Thread 8개로 10,000 CCU를 20ms 평균 지연시간으로 처리했습니다."

### Q2. "Game Tick Thread는 왜 분리했나요?"

**답변:**
"동시성 제어를 단순화하기 위해서입니다. 게임 오브젝트는 Game Tick Thread에서만 수정하므로
Lock 없이도 안전합니다. IOCP Worker들은 Job을 Push하기만 하고, Game Tick Thread가
Single-Writer로 모든 Job을 순차 실행합니다. 이렇게 하면 데드락이나 Race Condition 걱정이 없습니다."

### Q3. "Quadtree 대신 Grid를 쓰면 안 되나요?"

**답변:**
"Grid도 좋지만 Quadtree가 더 유연합니다.
- Grid: 고정 크기 셀, O(1) 삽입/삭제, 균등 분포에 유리
- Quadtree: 동적 분할, O(log n) 삽입/삭제, 불균등 분포에 유리

저희 게임은 던전/필드별로 몬스터 밀집도가 달라서 Quadtree를 선택했습니다.
던전(밀집)은 깊이 5까지 분할되고, 평원(sparse)은 깊이 2로 충분했습니다."

### Q4. "Lock-Free가 항상 빠른가요?"

**답변:**
"아닙니다. Lock-Free는 경합(contention)이 높을 때만 유리합니다.
우리 경우 8개 IOCP 스레드가 동시에 JobQueue에 Push하므로 Lock 경합이 심했습니다.
하지만 경합이 낮다면 Lock이 더 간단하고 빠를 수 있습니다.
또한 Lock-Free는 ABA 문제, Memory Ordering 등 구현이 복잡해서 디버깅이 어렵습니다."

### Q5. "서버 권위 모델이 뭔가요?"

**답변:**
"클라이언트를 신뢰하지 않는 원칙입니다. 클라이언트는 '의도'만 전송하고, 
모든 검증과 계산은 서버에서 수행합니다.

예를 들어 공격 시:
- 클라이언트: "몬스터 공격" (데미지 값 없음)
- 서버: 사거리, 쿨다운 검증 → 데미지 계산 → 결과 적용

이렇게 하면 메모리 해킹이나 패킷 변조로 치팅할 수 없습니다."

### Q6. "Lock-Free를 왜 사용했나요?"

**답변:**
"Zone의 JobQueue를 Lock-Free MPSC로 구현해서 4.7배 성능 향상을 달성했습니다.
여러 IOCP 스레드가 동시에 Job을 Push하는 상황에서 Lock 경합이 병목이었습니다.
CAS 연산과 Memory Ordering을 활용했고, ABA 문제는 Version Counter로 해결했습니다."

### Q7. "Dump 분석은 어떻게 하나요?"

**답변:**
"WinDbg를 사용합니다. `!analyze -v`로 크래시 지점을 파악하고, 
`dv` 명령으로 로컬 변수를 확인합니다.
Access Violation의 경우 nullptr 접근을 빠르게 찾을 수 있고,
Deadlock은 `!locks` 명령으로 Lock 순서를 분석해 Circular Wait를 발견했습니다."

### Q8. "성능 병목은 어떻게 찾았나요?"

**답변:**
"Visual Studio Profiler의 CPU Usage로 Hot Path를 확인했습니다.
RebuildQuadtree가 28%를 차지하는 것을 발견해 Dirty Flag 패턴으로 최적화했고,
CPU 사용률을 45%에서 22%로 절반 줄였습니다.
Memory Profiler로는 힙 스냅샷을 비교해서 Lambda의 순환 참조 메모리 릭을 찾아냈습니다."

### Q9. "C++20 Modules의 장점은?"

**답변:**
"컴파일 시간을 68% 단축했습니다 (5분 23초 → 1분 42초).
헤더 중복 포함 문제가 사라지고, 매크로 격리가 가능해졌습니다.
서드파티 라이브러리와의 호환성 문제는 Global Module Fragment로 해결했습니다."

### Q10. "왜 Crash Server를 만들지 않았나요?"

**답변:**
"로컬 파일 기반으로 충분합니다. MiniDump와 crash.log로 모든 디버깅 정보를 확보할 수 있고,
외부 의존성이 없어 심플합니다.
실무에서는 회사의 모니터링 시스템에 통합하면 되므로, 범용적인 파일 기반이 더 유연합니다."

### Q11. "DB 동기화는 어떻게 하나요?"

**답변:**
"Write-Back 캐시 방식을 사용합니다. 게임 오브젝트의 변경사항을 메모리에 누적하고,
주기적으로(5분마다) 또는 특정 이벤트(로그아웃, 아이템 획득)에만 DB에 기록합니다.
Dirty Flag로 변경된 필드만 UPDATE하여 쿼리 수를 90% 줄였습니다.
서버 크래시에 대비해 최근 5분 데이터 손실은 허용하는 정책입니다."

### Q12. "패킷 손실은 어떻게 처리하나요?"

**답변:**
"TCP를 사용하므로 패킷 손실은 TCP 레벨에서 자동 재전송됩니다.
다만 패킷 순서 보장을 위해 Sequence Number를 사용합니다.
클라이언트가 보낸 패킷마다 증가하는 번호를 검증해서,
순서가 맞지 않으면 연결을 끊습니다. 이는 리플레이 공격 방지에도 효과적입니다."

### Q13. "Zone 간 이동은 어떻게 처리하나요?"

**답변:**
"현재는 단일 프로세스이므로 Zone 간 이동이 간단합니다.
1. 기존 Zone의 Quadtree에서 제거
2. Player의 Zone 포인터 변경
3. 새 Zone의 Quadtree에 추가
4. 주변 플레이어들에게 Spawn/Despawn 패킷 전송

추후 분산 환경에서는 Redis를 통한 Zone 간 메시지 전달이나
Player 데이터 직렬화/역직렬화가 필요합니다."

### Q14. "Delta Compression이 뭔가요?"

**답변:**
"이동 패킷을 보낼 때 절대 좌표 대신 '이전 위치와의 차이'만 전송하는 기법입니다.
예를 들어:
- 절대 좌표: (1234.567, 5678.901) = 8 bytes
- 차분 값: (+12, -5) = 2 bytes (int16)

40% 대역폭 절감 효과를 봤고, 특히 미세한 이동이 많은 MMORPG에 효과적입니다.
첫 패킷만 절대 좌표로 보내고, 이후엔 차분 값만 전송합니다."

### Q15. "Object Pool은 왜 TLS로 만들었나요?"

**답변:**
"Lock 없이 빠른 할당/해제를 위해서입니다.
각 스레드마다 독립적인 Pool을 가지므로 Lock이 필요 없고,
malloc/free 대비 12.5배 빠른 성능을 달성했습니다.
단점은 메모리 사용량이 늘어난다는 것인데, 우리는 8개 스레드 × 1000개 풀링 = 8000개 정도로
허용 가능한 수준이었습니다."

### Q16. "Behavior Tree vs FSM?"

**답변:**
"Behavior Tree가 더 직관적이고 확장 가능합니다.
FSM은 상태 전이가 복잡해지면 스파게티 코드가 되기 쉽지만,
Behavior Tree는 노드를 트리 구조로 조합해서 복잡한 AI를 구현할 수 있습니다.
JSON 파일로 정의해서 기획자가 직접 수정할 수 있게 했고,
디버깅 시 현재 실행 중인 노드를 시각화할 수 있어 편리합니다."

### Q17. "A* vs Dijkstra?"

**답변:**
"A*가 더 빠릅니다. Dijkstra는 모든 방향을 탐색하지만,
A*는 휴리스틱(목표까지 직선 거리)을 사용해 목표 방향을 우선 탐색합니다.
우리는 JPS(Jump Point Search)를 추가로 적용해 대칭 경로를 건너뛰어
일반 A* 대비 10배 빠른 성능을 냈습니다.
64x64 맵에서 평균 탐색 노드 수가 400개 → 40개로 줄었습니다."

### Q18. "브로드캐스트 최적화는?"

**답변:**
"세 가지 기법을 사용했습니다:
1. Quadtree로 시야 범위만 검색 (O(n) → O(log n))
2. 패킷 재사용 - SendBuffer를 한 번만 생성하고 공유
3. Packet Aggregation - 100ms 동안 여러 패킷을 모아서 한 번에 전송

결과적으로 네트워크 대역폭을 40% 줄이고, CPU 사용률을 18%로 낮췄습니다."

### Q19. "세션 관리는 어떻게 하나요?"

**답변:**
"shared_ptr로 세션의 생명주기를 관리합니다.
- IOCP Completion 콜백에서 shared_ptr 보유
- 연결 종료 시 자동으로 정리
- weak_ptr로 순환 참조 방지

연결 종료 시 OnDisconnected()에서:
1. Zone에서 Player 제거
2. DB에 마지막 상태 저장
3. 주변 플레이어들에게 Despawn 알림
4. 세션 리소스 정리"

### Q20. "보안 취약점 대응은?"

**답변:**
"세 가지 레이어로 방어합니다:
1. 네트워크: 패킷 크기, Sequence 검증으로 잘못된 패킷 차단
2. 로직: 쿨다운, 사거리, 이동 속도 서버에서 검증
3. 모니터링: 비정상 행동 패턴 감지 (1초에 100번 공격 등)

실제로 스피드핵 시도를 감지해서 자동으로 연결을 끊은 적이 있습니다.
이동 속도가 최대치의 3배를 넘으면 의심 로그를 남기고 연결을 종료합니다."

---

## 참고 자료

### IOCP & Networking
- [Microsoft IOCP Documentation](https://docs.microsoft.com/en-us/windows/win32/fileio/i-o-completion-ports)
- [IOCP Echo Server Example](https://github.com/microsoft/Windows-classic-samples/tree/main/Samples/Win7Samples/netds/winsock/iocp)
- [Winsock2 Documentation](https://docs.microsoft.com/en-us/windows/win32/winsock/windows-sockets-start-page-2)

### Concurrency
- [Lock-Free Programming](https://preshing.com/20120612/an-introduction-to-lock-free-programming/)
- [CAS Operations](https://en.wikipedia.org/wiki/Compare-and-swap)
- [Memory Ordering](https://en.cppreference.com/w/cpp/atomic/memory_order)

### 알고리즘 & 자료구조
- [Quadtree - Wikipedia](https://en.wikipedia.org/wiki/Quadtree)
- [A* Pathfinding](https://www.redblobgames.com/pathfinding/a-star/introduction.html)
- [Jump Point Search](https://zerowidth.com/2013/a-visual-explanation-of-jump-point-search.html)

### C++ Modern Features
- [C++20 Modules](https://en.cppreference.com/w/cpp/language/modules)
- [C++20 Concepts](https://en.cppreference.com/w/cpp/language/constraints)
- [C++20 Coroutines](https://en.cppreference.com/w/cpp/language/coroutines)

### 디버깅 & 프로파일링
- [WinDbg Documentation](https://docs.microsoft.com/en-us/windows-hardware/drivers/debugger/)
- [Visual Studio Profiler](https://docs.microsoft.com/en-us/visualstudio/profiling/)
- [ETW (Event Tracing for Windows)](https://docs.microsoft.com/en-us/windows/win32/etw/about-event-tracing)

### 게임 서버 아키텍처
- [Inflearn - Rookiss 게임 서버 강의](https://www.inflearn.com/course/유니티-mmorpg-개발-part4)
- [GameNetworkingSockets (Valve)](https://github.com/ValveSoftware/GameNetworkingSockets)
- [Behavior Tree for AI](https://www.gamedeveloper.com/programming/behavior-trees-for-ai-how-they-work)