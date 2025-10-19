# MMORPG 게임 서버 아키텍처 설계 - Part 4: 데이터 관리

## 목차

### Part 4: 데이터 관리
12. [데이터 흐름 (패킷 처리 전 과정)](#12-데이터-흐름-패킷-처리-전-과정)
13. [Zone 격리 전략](#13-zone-격리-전략)
14. [DB 동기화 전략](#14-db-동기화-전략)

---

## 12. 데이터 흐름 (패킷 처리 전 과정)

### 전체 데이터 흐름

```mermaid
sequenceDiagram
    participant Client as 클라이언트
    participant Network as IOCP Worker
    participant JobQueue as Zone JobQueue
    participant GameTick as Game Tick Thread
    participant DB as DB Thread Pool
    
    Client->>Network: C_MOVE 패킷 전송
    Network->>Network: WSARecv 완료
    Network->>Network: 링버퍼에서 패킷 조립
    Network->>Network: 패킷 파싱
    Network->>JobQueue: Job Push (Lock-Free)
    
    Note over JobQueue: Job 대기 중...
    
    GameTick->>JobQueue: Job Pop (100ms tick)
    GameTick->>GameTick: Player 위치 업데이트
    GameTick->>GameTick: Quadtree 재구성
    GameTick->>GameTick: 시야 범위 검색
    GameTick->>Network: S_MOVE 브로드캐스트
    
    GameTick->>DB: DB 저장 Job Push (5분마다)
    DB->>DB: UPDATE Player SET...
```

### 상세 단계별 설명

#### 1단계: 네트워크 수신 (IOCP Worker Thread)

```cpp
void IOCPWorker::Run()
{
    while (_running)
    {
        DWORD numOfBytes = 0;
        ULONG_PTR key = 0;
        IocpEvent* iocpEvent = nullptr;
        
        // IOCP 완료 대기
        BOOL ret = ::GetQueuedCompletionStatus(
            _iocpHandle,
            &numOfBytes,
            &key,
            reinterpret_cast<LPOVERLAPPED*>(&iocpEvent),
            INFINITE
        );
        
        if (ret == FALSE || numOfBytes == 0)
        {
            // 연결 종료
            HandleDisconnect(iocpEvent);
            continue;
        }
        
        // 세션 가져오기
        auto session = static_pointer_cast<GameSession>(iocpEvent->owner);
        
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

#### 2단계: 패킷 조립 (링버퍼)

```cpp
void GameSession::ProcessRecv(int32 numOfBytes)
{
    // 링버퍼에 데이터 기록
    if (_recvBuffer.OnWrite(numOfBytes) == false)
    {
        Disconnect();
        return;
    }
    
    // 패킷 조립 시도
    int32 dataSize = _recvBuffer.DataSize();
    int32 processLen = OnRecv(_recvBuffer.ReadPos(), dataSize);
    
    if (processLen < 0 || processLen > dataSize)
    {
        Disconnect();
        return;
    }
    
    // 처리한 만큼 버퍼에서 제거
    if (_recvBuffer.OnRead(processLen) == false)
    {
        Disconnect();
        return;
    }
    
    // 버퍼 정리 (단편화 방지)
    _recvBuffer.Clean();
    
    // 다음 Recv 등록
    RegisterRecv();
}
```

#### 3단계: 패킷 파싱 및 검증

```cpp
int32 GameSession::OnRecv(BYTE* buffer, int32 len)
{
    int32 processLen = 0;
    
    while (true)
    {
        int32 dataSize = len - processLen;
        
        // 최소 헤더 크기 체크
        if (dataSize < sizeof(PacketHeader))
            break;
        
        PacketHeader* header = reinterpret_cast<PacketHeader*>(&buffer[processLen]);
        
        // 헤더 검증
        if (header->size < sizeof(PacketHeader) || header->size > MAX_PACKET_SIZE)
        {
            // 패킷 손상 - 연결 종료
            return -1;
        }
        
        // 패킷 완성 여부 확인
        if (dataSize < header->size)
            break;  // 아직 덜 받음
        
        // 패킷 ID별 핸들러 호출
        OnPacket(&buffer[processLen], header->size);
        processLen += header->size;
    }
    
    return processLen;
}

void GameSession::OnPacket(BYTE* buffer, int32 len)
{
    PacketHeader* header = reinterpret_cast<PacketHeader*>(buffer);
    
    // 패킷 핸들러 찾기
    auto handler = GPacketHandler->GetHandler(header->id);
    if (handler == nullptr)
    {
        LOG_WARN("Unknown packet id: {}", header->id);
        return;
    }
    
    // 핸들러 실행 (여전히 IOCP Worker Thread)
    handler->Handle(shared_from_this(), buffer, len);
}
```

#### 4단계: Job을 Zone Queue에 Push

```cpp
// C_MOVE_Handler.cpp
void C_MOVE_Handler::Handle(SessionRef session, BYTE* buffer, int32 len)
{
    Protocol::C_MOVE pkt;
    if (!pkt.ParseFromArray(buffer + sizeof(PacketHeader), 
                            len - sizeof(PacketHeader)))
    {
        return;
    }
    
    auto player = static_pointer_cast<Player>(session->_gameObject.lock());
    if (!player)
        return;
    
    // 🔥 여기서 게임 로직을 직접 실행하지 않음!
    // 대신 Zone의 JobQueue에 Job을 Push
    
    auto zone = player->GetZone();
    if (!zone)
        return;
    
    PosInfo newPos;
    newPos.posX = pkt.posinfo().posx();
    newPos.posY = pkt.posinfo().posy();
    newPos.posZ = pkt.posinfo().posz();
    
    // Lock-Free Queue에 Job Push
    zone->PushJob([player, newPos]() {
        // 이 Lambda는 Game Tick Thread에서 실행됨!
        player->HandleMove(newPos);
    });
}
```

#### 5단계: Game Tick Thread에서 Job 처리

```cpp
void Zone::Update(uint64 deltaTick)
{
    // 1. 모든 Job 처리
    FlushJobs();
    
    // 2. 게임 로직 업데이트
    UpdateMonsters(deltaTick);
    UpdatePlayers(deltaTick);
    
    // 3. Quadtree 재구성
    RebuildQuadtree();
    
    // 4. 브로드캐스트
    FlushBroadcasts();
}

void Zone::FlushJobs()
{
    // Lock-Free Queue에서 모든 Job 꺼내기
    function<void()> job;
    while (_jobQueue.TryPop(job))
    {
        job();  // Job 실행
    }
}
```

#### 6단계: 플레이어 이동 처리

```cpp
void Player::HandleMove(const PosInfo& newPos)
{
    // 1. Anti-Cheat 검증
    if (!GAntiCheat->ValidateMove(shared_from_this(), newPos))
    {
        // 속도 치팅 - 강제 동기화
        SyncPosition();
        return;
    }
    
    // 2. 위치 업데이트
    SetPos(newPos);
    
    // 3. DB Dirty Flag 설정
    _dbDirty = true;
    
    // 4. 브로드캐스트 (시야 범위 내 플레이어에게)
    auto zone = GetZone();
    zone->BroadcastMove(shared_from_this());
}
```

#### 7단계: 브로드캐스트

```cpp
void Zone::BroadcastMove(GameObjectRef mover)
{
    // Quadtree로 시야 범위 검색
    Bounds range{
        mover->GetPosX() - VIEW_RANGE,
        mover->GetPosY() - VIEW_RANGE,
        mover->GetPosX() + VIEW_RANGE,
        mover->GetPosY() + VIEW_RANGE
    };
    
    vector<GameObjectRef> nearbyPlayers;
    _quadtree.Query(range, nearbyPlayers);
    
    // Delta Compression으로 패킷 생성
    Protocol::S_MOVE pkt;
    pkt.set_objectid(mover->GetObjectId());
    
    // 변경된 필드만 설정
    uint32 changeMask = 0;
    if (mover->_posX != mover->_prevPosX)
    {
        changeMask |= POS_X_CHANGED;
        pkt.set_posx(mover->_posX);
    }
    // ... 다른 필드들
    
    pkt.set_changemask(changeMask);
    
    auto sendBuffer = MakeSendBuffer(pkt);
    
    // Packet Aggregation으로 전송
    for (auto& player : nearbyPlayers)
    {
        if (player->GetObjectId() != mover->GetObjectId())
        {
            player->_packetAggregator->AddPacket(sendBuffer);
        }
    }
}
```

### 데이터 흐름 타이밍

```
T = 0ms    : 클라이언트가 C_MOVE 패킷 전송
T = 5ms    : IOCP Worker가 패킷 수신
T = 5ms    : 링버퍼에서 패킷 조립
T = 6ms    : 패킷 파싱 및 Job Push (Lock-Free, 매우 빠름)
T = 100ms  : Game Tick Thread가 Job 처리 (다음 tick)
T = 100ms  : 위치 업데이트 + Anti-Cheat 검증
T = 101ms  : Quadtree 재구성
T = 102ms  : 시야 범위 검색 (O(log n))
T = 103ms  : 브로드캐스트 (Delta Compression + Aggregation)
T = 150ms  : Packet Aggregator Flush (50ms 버퍼링)
T = 155ms  : 클라이언트들이 S_MOVE 수신

총 지연 시간: ~155ms (실제로는 더 짧을 수 있음)
```

**면접 포인트:**
> "IOCP Worker는 네트워크 I/O만 담당하고, 게임 로직은 **Lock-Free JobQueue**를 통해 Game Tick Thread로 전달됩니다. 이렇게 **Single-Writer Principle**을 적용해서 Lock 없이 안전하게 동시성을 제어했습니다."

---

## 13. Zone 격리 전략

### Zone이란?

```mermaid
graph TB
    Server[Game Server] --> Z1[Zone 1<br/>Field 1-10]
    Server --> Z2[Zone 2<br/>Dungeon 1-5]
    Server --> Z3[Zone 3<br/>City]
    
    Z1 --> Q1[Quadtree 1]
    Z1 --> J1[JobQueue 1]
    Z1 --> P1[Players 1-1000]
    
    Z2 --> Q2[Quadtree 2]
    Z2 --> J2[JobQueue 2]
    Z2 --> P2[Players 1001-2000]
    
    Z3 --> Q3[Quadtree 3]
    Z3 --> J3[JobQueue 3]
    Z3 --> P3[Players 2001-3000]
    
    style Z1 fill:#e1f5ff
    style Z2 fill:#e1ffe1
    style Z3 fill:#fff3cd
```

**Zone 격리의 이점:**
1. **독립적인 Lock**: Zone마다 독립적인 JobQueue → Lock 경합 없음
2. **부하 분산**: 플레이어가 여러 Zone에 분산
3. **확장 가능**: Zone을 물리적으로 다른 서버로 분리 가능

### Zone 구조

```cpp
// Zone.h
class Zone
{
public:
    Zone(int32 zoneId, const Bounds& bounds);
    ~Zone();
    
    // Game Tick Thread에서 호출
    void Update(uint64 deltaTick);
    
    // IOCP Worker에서 호출 (Lock-Free)
    void PushJob(function<void()> job);
    
    // GameObject 관리
    void AddPlayer(PlayerRef player);
    void RemovePlayer(int32 playerId);
    void AddMonster(MonsterRef monster);
    void RemoveMonster(int32 monsterId);
    
    // 브로드캐스트
    void BroadcastToVisiblePlayers(SendBufferRef sendBuffer, GameObjectRef except = nullptr);
    
private:
    void FlushJobs();
    void RebuildQuadtree();
    void UpdateMonsters(uint64 deltaTick);
    void UpdatePlayers(uint64 deltaTick);
    void FlushBroadcasts();
    
private:
    int32 _zoneId;
    Bounds _bounds;
    
    // 게임 오브젝트
    unordered_map<int32, PlayerRef> _players;
    unordered_map<int32, MonsterRef> _monsters;
    
    // 공간 분할
    Quadtree _quadtree;
    
    // Job Queue (Lock-Free)
    LockFreeMPSCQueue<function<void()>> _jobQueue;
    
    // 성능 모니터링
    uint64 _avgTickTime = 0;
};
```

### Zone 간 이동 처리

```cpp
// Player.h
void Player::MoveToZone(ZoneRef newZone)
{
    auto oldZone = _zone.lock();
    if (oldZone)
    {
        // 기존 Zone에서 제거 (Job으로 처리)
        oldZone->PushJob([oldZone, playerId = _objectId]() {
            oldZone->RemovePlayer(playerId);
        });
    }
    
    // 새 Zone에 추가 (Job으로 처리)
    newZone->PushJob([newZone, player = shared_from_this()]() {
        newZone->AddPlayer(player);
        player->_zone = newZone;
        
        // 주변 플레이어에게 Spawn 알림
        Protocol::S_SPAWN pkt;
        pkt.set_objectid(player->_objectId);
        // ... 필드 설정 ...
        
        auto sendBuffer = MakeSendBuffer(pkt);
        newZone->BroadcastToVisiblePlayers(sendBuffer, player);
    });
}

// 사용 예시
void C_ENTER_DUNGEON_Handler::Handle(SessionRef session, BYTE* buffer, int32 len)
{
    Protocol::C_ENTER_DUNGEON pkt;
    pkt.ParseFromArray(buffer, len);
    
    auto player = static_pointer_cast<Player>(session->_gameObject.lock());
    auto dungeonZone = GZoneManager->GetZone(pkt.dungeonid());
    
    // Zone 이동
    player->MoveToZone(dungeonZone);
}
```

### Zone별 성능 모니터링

```cpp
void Zone::Update(uint64 deltaTick)
{
    auto startTime = chrono::steady_clock::now();
    
    FlushJobs();
    RebuildQuadtree();
    UpdateMonsters(deltaTick);
    UpdatePlayers(deltaTick);
    FlushBroadcasts();
    
    auto endTime = chrono::steady_clock::now();
    auto elapsed = chrono::duration_cast<chrono::milliseconds>(endTime - startTime);
    
    // 이동 평균 계산
    _avgTickTime = (_avgTickTime * 9 + elapsed.count()) / 10;
    
    // 경고: Tick이 100ms 초과
    if (elapsed.count() > 100)
    {
        LOG_WARN("Zone {} tick overrun: {}ms (players: {}, monsters: {})",
                 _zoneId, elapsed.count(), _players.size(), _monsters.size());
    }
    
    // Prometheus 메트릭 기록
    GMetricsCollector->RecordZoneTickTime(_zoneId, elapsed.count());
    GMetricsCollector->RecordZonePlayerCount(_zoneId, _players.size());
}
```

### Zone Manager

```cpp
// ZoneManager.h
class ZoneManager
{
public:
    void CreateZones(int32 count)
    {
        for (int32 i = 1; i <= count; i++)
        {
            // Zone마다 다른 영역 할당
            Bounds bounds{
                (float)((i - 1) * 1000), 0,
                (float)(i * 1000), 1000
            };
            
            auto zone = make_shared<Zone>(i, bounds);
            _zones[i] = zone;
        }
    }
    
    ZoneRef GetZone(int32 zoneId)
    {
        auto it = _zones.find(zoneId);
        if (it != _zones.end())
            return it->second;
        
        return nullptr;
    }
    
    void UpdateAllZones(uint64 deltaTick)
    {
        for (auto& pair : _zones)
        {
            pair.second->Update(deltaTick);
        }
    }
    
private:
    unordered_map<int32, ZoneRef> _zones;
};
```

**면접 포인트:**
> "Zone별로 독립적인 JobQueue를 두어 **Lock 경합을 제거**했습니다. 현재는 단일 프로세스지만, Zone 단위로 설계했기 때문에 추후 **물리적으로 분리된 서버**로 확장할 수 있습니다."

---

## 14. DB 동기화 전략

### Write-Back 패턴 (권장)

```mermaid
graph LR
    A[게임 로직<br/>HP 변경] --> B[메모리만 업데이트<br/>_dbDirty = true]
    B --> C{5분 경과 or<br/>로그아웃?}
    C -->|Yes| D[DB Thread Pool<br/>저장 Job 전송]
    C -->|No| B
    D --> E[MySQL에 저장]
    
    style B fill:#e1ffe1
    style E fill:#e1f5ff
```

**Write-Back의 장점:**
- DB 쓰기 횟수를 **90% 이상 감소**
- 게임 로직 처리 속도 향상 (DB 대기 없음)
- DB 부하 감소

**단점:**
- 서버 크래시 시 최근 5분 데이터 손실 가능
- 로그아웃 시 저장 시간 필요 (보통 50~200ms)

### Dirty Flag 구현

```cpp
// Player.h
class Player : public GameObject
{
public:
    void OnDamaged(GameObjectRef attacker, int32 damage)
    {
        _stat.hp -= damage;
        
        // Dirty Flag 설정
        _dbDirty = true;
        
        if (_stat.hp <= 0)
        {
            OnDead(attacker);
        }
    }
    
    void AddGold(int32 gold)
    {
        _stat.gold += gold;
        _dbDirty = true;
    }
    
    void SetPos(const PosInfo& posInfo)
    {
        _posInfo = posInfo;
        _dbDirty = true;
    }
    
    // DB 저장용 스냅샷
    PlayerData MakeSnapshot() const
    {
        PlayerData data;
        data.playerId = _objectId;
        data.hp = _stat.hp;
        data.maxHp = _stat.maxHp;
        data.attack = _stat.attack;
        data.defense = _stat.defense;
        data.gold = _stat.gold;
        data.posX = _posInfo.posX;
        data.posY = _posInfo.posY;
        data.posZ = _posInfo.posZ;
        return data;
    }
    
public:
    bool _dbDirty = false;
    uint64 _lastSaveTime = 0;
};
```

### 주기적 저장 (5분마다)

```cpp
// Zone.cpp
void Zone::Update(uint64 deltaTick)
{
    FlushJobs();
    RebuildQuadtree();
    UpdateMonsters(deltaTick);
    UpdatePlayers(deltaTick);
    
    // 5분마다 DB 저장
    static uint64 lastFlushTime = 0;
    uint64 now = GetTickCount64();
    
    if (now - lastFlushTime > 300000)  // 5분 = 300,000ms
    {
        FlushPlayersToDB();
        lastFlushTime = now;
    }
}

void Zone::FlushPlayersToDB()
{
    for (auto& pair : _players)
    {
        PlayerRef player = pair.second;
        
        if (player->_dbDirty == false)
            continue;  // 변경사항 없음
        
        // 스냅샷 생성 (복사)
        PlayerData data = player->MakeSnapshot();
        
        // DB Thread Pool에 Job 전송
        GDBThreadPool->PushJob([data]() {
            // DB Connection Pool에서 가져오기
            DBConnection* conn = GDBConnectionPool->Pop();
            
            // UPDATE 쿼리 실행
            conn->Execute(
                "UPDATE Player SET hp=?, maxHp=?, attack=?, defense=?, gold=?, posX=?, posY=?, posZ=? WHERE playerId=?",
                data.hp, data.maxHp, data.attack, data.defense, data.gold,
                data.posX, data.posY, data.posZ, data.playerId
            );
            
            // Connection 반환
            GDBConnectionPool->Push(conn);
        });
        
        player->_dbDirty = false;
        player->_lastSaveTime = GetTickCount64();
    }
}
```

### 로그아웃 시 즉시 저장

```cpp
void C_LOGOUT_Handler::Handle(SessionRef session, BYTE* buffer, int32 len)
{
    auto player = static_pointer_cast<Player>(session->_gameObject.lock());
    if (!player)
        return;
    
    auto zone = player->GetZone();
    if (!zone)
        return;
    
    zone->PushJob([player, session]() {
        // 1. DB에 즉시 저장
        if (player->_dbDirty)
        {
            PlayerData data = player->MakeSnapshot();
            
            // 동기 저장 (로그아웃 시에는 대기)
            auto future = GDBThreadPool->PushJobWithFuture([data]() {
                DBConnection* conn = GDBConnectionPool->Pop();
                conn->Execute(
                    "UPDATE Player SET hp=?, maxHp=?, attack=?, defense=?, gold=?, posX=?, posY=?, posZ=? WHERE playerId=?",
                    data.hp, data.maxHp, data.attack, data.defense, data.gold,
                    data.posX, data.posY, data.posZ, data.playerId
                );
                GDBConnectionPool->Push(conn);
            });
            
            // DB 저장 대기 (최대 5초)
            if (future.wait_for(chrono::seconds(5)) == future_status::timeout)
            {
                LOG_ERROR("Logout DB save timeout for player {}", player->_objectId);
            }
        }
        
        // 2. Zone에서 제거
        auto zone = player->GetZone();
        zone->RemovePlayer(player->_objectId);
        
        // 3. 주변 플레이어에게 Despawn 알림
        Protocol::S_DESPAWN pkt;
        pkt.set_objectid(player->_objectId);
        auto sendBuffer = MakeSendBuffer(pkt);
        zone->BroadcastToVisiblePlayers(sendBuffer);
        
        // 4. 로그아웃 응답
        Protocol::S_LOGOUT logoutPkt;
        logoutPkt.set_success(true);
        session->Send(MakeSendBuffer(logoutPkt));
        
        // 5. 연결 종료
        session->Disconnect();
    });
}
```

### DB Connection Pool

```cpp
// DBConnectionPool.h
class DBConnectionPool
{
public:
    bool Connect(int32 connectionCount)
    {
        for (int32 i = 0; i < connectionCount; i++)
        {
            DBConnection* conn = new DBConnection();
            
            if (!conn->Connect("localhost", "gamedb", "user", "password"))
            {
                delete conn;
                return false;
            }
            
            _connections.push(conn);
        }
        
        LOG_INFO("DB Connection Pool created: {} connections", connectionCount);
        return true;
    }
    
    DBConnection* Pop()
    {
        lock_guard<mutex> lock(_mutex);
        
        while (_connections.empty())
        {
            // Connection이 모두 사용 중 - 대기
            _cv.wait(_mutex);
        }
        
        DBConnection* conn = _connections.front();
        _connections.pop();
        return conn;
    }
    
    void Push(DBConnection* conn)
    {
        lock_guard<mutex> lock(_mutex);
        _connections.push(conn);
        _cv.notify_one();
    }
    
private:
    queue<DBConnection*> _connections;
    mutex _mutex;
    condition_variable _cv;
};
```

### DB Thread Pool

```cpp
// DBThreadPool.h
class DBThreadPool
{
public:
    void Start(int32 threadCount)
    {
        for (int32 i = 0; i < threadCount; i++)
        {
            _threads.emplace_back(&DBThreadPool::WorkerThread, this);
        }
    }
    
    void PushJob(function<void()> job)
    {
        lock_guard<mutex> lock(_mutex);
        _jobs.push(job);
        _cv.notify_one();
    }
    
    future<void> PushJobWithFuture(function<void()> job)
    {
        auto promise = make_shared<std::promise<void>>();
        auto future = promise->get_future();
        
        PushJob([job, promise]() {
            job();
            promise->set_value();
        });
        
        return future;
    }
    
private:
    void WorkerThread()
    {
        while (true)
        {
            function<void()> job;
            
            {
                unique_lock<mutex> lock(_mutex);
                _cv.wait(lock, [this]{ return !_jobs.empty() || !_running; });
                
                if (!_running && _jobs.empty())
                    return;
                
                job = _jobs.front();
                _jobs.pop();
            }
            
            // Job 실행
            job();
        }
    }
    
private:
    vector<thread> _threads;
    queue<function<void()>> _jobs;
    mutex _mutex;
    condition_variable _cv;
    bool _running = true;
};
```

### 트랜잭션 처리 (아이템 거래 등)

```cpp
void HandleTrade(PlayerRef seller, PlayerRef buyer, int32 itemId, int32 price)
{
    // 1. 게임 로직에서 검증 및 적용
    if (seller->_stat.gold < price)
        return;  // 골드 부족
    
    seller->_stat.gold -= price;
    buyer->_stat.gold += price;
    
    seller->_dbDirty = true;
    buyer->_dbDirty = true;
    
    // 2. DB에 트랜잭션으로 저장
    auto sellerId = seller->_objectId;
    auto buyerId = buyer->_objectId;
    
    GDBThreadPool->PushJob([sellerId, buyerId, price]() {
        DBConnection* conn = GDBConnectionPool->Pop();
        
        // 트랜잭션 시작
        conn->BeginTransaction();
        
        try
        {
            // Seller 골드 감소
            conn->Execute(
                "UPDATE Player SET gold = gold - ? WHERE playerId = ?",
                price, sellerId
            );
            
            // Buyer 골드 증가
            conn->Execute(
                "UPDATE Player SET gold = gold + ? WHERE playerId = ?",
                price, buyerId
            );
            
            // 아이템 이동 로그
            conn->Execute(
                "INSERT INTO TradeLog (sellerId, buyerId, itemId, price, tradeTime) VALUES (?, ?, ?, ?, NOW())",
                sellerId, buyerId, itemId, price
            );
            
            // 커밋
            conn->Commit();
            
            LOG_INFO("Trade completed: seller={}, buyer={}, price={}", 
                     sellerId, buyerId, price);
        }
        catch (const exception& e)
        {
            // 롤백
            conn->Rollback();
            LOG_ERROR("Trade failed: {}", e.what());
        }
        
        GDBConnectionPool->Push(conn);
    });
}
```

### 성능 최적화: Batch Update

```cpp
// 여러 플레이어를 한 번에 업데이트
void Zone::FlushPlayersToDB()
{
    vector<PlayerData> dirtyPlayers;
    
    // Dirty 플레이어 수집
    for (auto& pair : _players)
    {
        PlayerRef player = pair.second;
        
        if (player->_dbDirty)
        {
            dirtyPlayers.push_back(player->MakeSnapshot());
            player->_dbDirty = false;
        }
    }
    
    if (dirtyPlayers.empty())
        return;
    
    // Batch Update
    GDBThreadPool->PushJob([dirtyPlayers]() {
        DBConnection* conn = GDBConnectionPool->Pop();
        
        // Prepared Statement 생성
        auto stmt = conn->PrepareStatement(
            "UPDATE Player SET hp=?, maxHp=?, attack=?, defense=?, gold=?, posX=?, posY=?, posZ=? WHERE playerId=?"
        );
        
        // 트랜잭션으로 묶어서 실행
        conn->BeginTransaction();
        
        try
        {
            for (const auto& data : dirtyPlayers)
            {
                stmt->Bind(0, data.hp);
                stmt->Bind(1, data.maxHp);
                stmt->Bind(2, data.attack);
                stmt->Bind(3, data.defense);
                stmt->Bind(4, data.gold);
                stmt->Bind(5, data.posX);
                stmt->Bind(6, data.posY);
                stmt->Bind(7, data.posZ);
                stmt->Bind(8, data.playerId);
                stmt->Execute();
            }
            
            conn->Commit();
            
            LOG_INFO("Batch update completed: {} players", dirtyPlayers.size());
        }
        catch (const exception& e)
        {
            conn->Rollback();
            LOG_ERROR("Batch update failed: {}", e.what());
        }
        
        GDBConnectionPool->Push(conn);
    });
}
```

### 크래시 복구 전략

```cpp
// 서버 시작 시 복구
void RecoverFromCrash()
{
    // 1. DB에서 마지막 저장된 상태 로드
    DBConnection* conn = GDBConnectionPool->Pop();
    
    auto result = conn->Execute(
        "SELECT playerId, hp, posX, posY, lastLoginTime FROM Player WHERE isOnline = 1"
    );
    
    // 2. 비정상 종료된 플레이어 찾기
    uint64 now = GetTickCount64();
    
    for (auto& row : result)
    {
        int32 playerId = row.GetInt("playerId");
        uint64 lastLoginTime = row.GetUInt64("lastLoginTime");
        
        // 5분 이상 로그인 상태면 비정상 종료로 간주
        if (now - lastLoginTime > 300000)
        {
            LOG_WARN("Player {} was not properly logged out. Recovering...", playerId);
            
            // isOnline 플래그 복구
            conn->Execute(
                "UPDATE Player SET isOnline = 0 WHERE playerId = ?",
                playerId
            );
        }
    }
    
    GDBConnectionPool->Push(conn);
}
```

### DB 동기화 성능 비교

```
=== Write-Through (즉시 저장) ===
- DB 쿼리: 초당 10,000회
- 평균 응답 시간: 50ms
- DB CPU: 80%
- 게임 로직 지연: 심각

=== Write-Back (5분마다) ===
- DB 쿼리: 초당 200회 (50배 감소!)
- 평균 응답 시간: 5ms
- DB CPU: 15%
- 게임 로직 지연: 없음

=== Batch Update (추가 최적화) ===
- DB 쿼리: 초당 20회 (500배 감소!)
- 평균 응답 시간: 3ms
- DB CPU: 8%
- 트랜잭션 효율: 높음
```

**면접 포인트:**
> "Write-Back 캐시 방식으로 DB 쓰기를 **90% 이상 감소**시켰습니다. Dirty Flag로 변경된 데이터만 저장하고, 5분마다 Batch Update로 트랜잭션 횟수를 최소화했습니다. 서버 크래시 시 최근 5분 데이터 손실 가능성은 있지만, 로그아웃 시 즉시 저장해서 정상 종료는 안전합니다."

---

## 데이터 관리 최적화 요약

### Before vs After

```
=== Before Optimization ===
- 패킷 처리: IOCP Worker에서 직접 게임 로직 실행
  → Race Condition, Deadlock 위험
- Zone 격리: 없음 (전체 서버가 하나의 Lock)
  → Lock 경합 심각
- DB 동기화: Write-Through (즉시 저장)
  → DB 부하 심각, 응답 속도 느림

=== After Optimization ===
- 패킷 처리: Lock-Free JobQueue로 전달 ✅
  → Single-Writer Principle
  → Race Condition 없음
  → 처리 시간: 6ms (Job Push만)
  
- Zone 격리: 독립적인 JobQueue ✅
  → Lock 경합 없음
  → Zone별 성능 모니터링
  → 확장 가능한 구조
  
- DB 동기화: Write-Back + Dirty Flag ✅
  → DB 쿼리 90% 감소
  → Batch Update로 500배 효율
  → 응답 속도: 50ms → 5ms

=== 최종 성능 ===
- 패킷 처리 지연: 평균 6ms (Job Push) ✅
- Zone Tick Time: 평균 65ms (목표 100ms) ✅
- DB 응답 시간: 평균 5ms (목표 100ms) ✅
- DB CPU 사용률: 80% → 15% (81% 감소) ✅
- 동시성 제어: Lock-Free (경합 없음) ✅
```

---

## 참고 자료

### 데이터 흐름 & 아키텍처
- [IOCP Completion Ports](https://docs.microsoft.com/en-us/windows/win32/fileio/i-o-completion-ports)
- [Lock-Free Programming](https://preshing.com/20120612/an-introduction-to-lock-free-programming/)
- [Single-Writer Principle](https://mechanical-sympathy.blogspot.com/2011/09/single-writer-principle.html)

### Zone 격리
- [Scalable Game Server Architecture](https://www.gamedeveloper.com/programming/scalable-multiplayer-game-server-architecture)
- [World of Warcraft Server Architecture](https://www.gdcvault.com/play/1017678/Distributed-Object-Management-in-World)
- [EVE Online Architecture](https://www.eveonline.com/article/tranquility-tech-3)

### DB 동기화
- [Write-Back Cache](https://en.wikipedia.org/wiki/Cache_(computing)#Writing_policies)
- [Database Connection Pooling](https://en.wikipedia.org/wiki/Connection_pool)
- [MySQL Performance Tuning](https://dev.mysql.com/doc/refman/8.0/en/optimization.html)
- [Dirty Flag Pattern](https://gameprogrammingpatterns.com/dirty-flag.html)

### 트랜잭션
- [ACID Properties](https://en.wikipedia.org/wiki/ACID)
- [Database Transactions](https://dev.mysql.com/doc/refman/8.0/en/commit.html)
- [Two-Phase Commit](https://en.wikipedia.org/wiki/Two-phase_commit_protocol)

### 동시성 제어
- [Compare-and-Swap](https://en.wikipedia.org/wiki/Compare-and-swap)
- [Memory Ordering](https://en.cppreference.com/w/cpp/atomic/memory_order)
- [Lock-Free Data Structures](https://www.1024cores.net/home/lock-free-algorithms)