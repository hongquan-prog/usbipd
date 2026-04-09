# USBIPD 多客户端架构流程图

## 1. 整体架构图

```mermaid
flowchart TB
    subgraph InitializationPhase["1.初始化阶段 (usbip_server_init)"]
        direction TB
        S_INIT["usbip_server_init()"] --> CM_INIT["usbip_conn_manager_init()"]
        CM_INIT --> T_LISTEN["transport_listen()"]
        T_LISTEN --> S_INIT_OK["Server Ready"]
    end

    subgraph RuntimePhase["2.运行阶段 (usbip_server_run)"]
        direction TB
        S_RUN["usbip_server_run()"] --> S_ACCEPT["transport_accept()"]
        S_ACCEPT --> S_HANDLE["handle_single_op()"]
        S_HANDLE --> S_DEVLIST{"OP_REQ_DEVLIST?"}
        S_DEVLIST -->|Yes| S_SEND_DEV["Send Device List"]
        S_DEVLIST -->|No| S_IMPORT{"OP_REQ_IMPORT?"}
        S_IMPORT -->|Yes| S_IMPORT_HANDLE["usbip_server_handle_import_req()"]
        S_IMPORT -->|No| S_CLOSE["Close Connection"]
        S_SEND_DEV --> S_CLOSE
        S_CLOSE --> S_RUN
    end

    subgraph ConnectionCreation["3.连接创建阶段 (On IMPORT Request)"]
        direction TB
        IMPORT_START["Handle Import"] --> CHECK_DEV["Check Device Available"]
        CHECK_DEV --> SEND_OK["Send Import OK"]
        SEND_OK --> CM_CREATE["usbip_connection_create()"]
        CM_CREATE --> CM_ADD["usbip_conn_manager_add()"]
        CM_ADD --> DM_BIND["usbip_bind_device()"]
        DM_BIND --> DEV_EXPORT["driver->export_device()"]
        DEV_EXPORT --> CM_START["usbip_connection_start()"]
    end

    subgraph PerConnection["4.每连接处理阶段"]
        direction TB
        CM_START_EVT["Start Threads"] --> Q_INIT["usbip_urb_queue_init()"]
        Q_INIT --> PROC_CREATE["Create Processor Thread"]
        PROC_CREATE --> RX_CREATE["Create RX Thread"]
        RX_CREATE --> ACTIVE["CONN_STATE_ACTIVE"]
        
        subgraph RXThread["RX Thread"]
            RX_LOOP["Receive URB"] --> Q_PUSH["usbip_urb_queue_push()"]
            Q_PUSH --> RX_LOOP
        end
        
        subgraph ProcessorThread["Processor Thread"]
            PROC_LOOP["usbip_urb_queue_pop()"] --> HANDLE["driver->handle_urb()"]
            HANDLE --> SEND["Send Response"]
            SEND --> PROC_LOOP
        end
        
        ACTIVE -.-> RXThread
        ACTIVE -.-> ProcessorThread
    end

    subgraph CleanupPhase["5.清理阶段"]
        direction TB
        CM_STOP["usbip_connection_stop()"] --> Q_CLOSE["usbip_urb_queue_close()"]
        Q_CLOSE --> JOIN_PROC["Join Threads"]
        JOIN_PROC --> Q_DESTROY["usbip_urb_queue_destroy()"]
        Q_DESTROY --> CM_DESTROY["usbip_connection_destroy()"]
        CM_DESTROY --> CM_REMOVE["usbip_conn_manager_remove()"]
        CM_REMOVE --> DM_UNBIND["usbip_unbind_device()"]
    end

    subgraph StaticComponents["静态组件 (程序启动时)"]
        direction TB
        DM_REG["usbip_register_driver()<br/>(constructor)"] --> HID["hid_dap.c"]
        DM_REG --> BULK["bulk_dap.c"]
    end

    %% 阶段之间的连接 - 主流程
    S_INIT_OK --> S_RUN
    S_IMPORT_HANDLE -.-> IMPORT_START
    CM_START -.-> CM_START_EVT
    
    %% 阶段5的入口连接（清理触发条件）
    %% 1. 连接断开/错误时进入清理
    RXThread -.->|Connection Error / Disconnect| CM_STOP
    ProcessorThread -.->|Connection Error| CM_STOP
    
    %% 2. 连接正常关闭时进入清理
    S_CLOSE -.->|Connection Closed| CM_STOP
    
    %% 3. 服务器关闭时进入清理
    RuntimePhase -.->|Server Stop| CleanupPhase
    
    %% 4. 清理后资源释放
    DM_UNBIND --> DEV_UNEXPORT["driver->unexport_device()"]
    DEV_UNEXPORT --> DEV_AVAIL["Device Available Again"]
    
    %% 样式
    style InitializationPhase fill:#e3f2fd
    style RuntimePhase fill:#e8f5e9
    style ConnectionCreation fill:#fff3e0
    style PerConnection fill:#f3e5f5
    style CleanupPhase fill:#ffebee
    style StaticComponents fill:#f5f5f5
    style DEV_AVAIL fill:#c8e6c9
```

## 2. 服务器主循环流程

```mermaid
flowchart TD
    A[Start Server] --> B[usbip_server_init]
    B --> B1[Initialize Connection Manager]
    B --> B2[Start Transport Listen]
    B --> B3[Register Device Drivers]
    B --> C[usbip_server_run]
    
    C --> D{Server Running?}
    D -->|Yes| E[transport_accept]
    D -->|No| Z[Cleanup]
    
    E --> F{Connection Accepted?}
    F -->|No| D
    F -->|Yes| G[Handle Single Operation]
    
    G --> H[Receive OP Header]
    H --> I{Operation Type}
    
    I -->|DEVLIST| J[Send Device List]
    J --> K[Close Connection]
    K --> D
    
    I -->|IMPORT| L[Handle Import Request]
    L --> M[Find Device]
    M --> N{Device Available?}
    N -->|No| O[Send BUSY/ERROR]
    O --> K
    
    N -->|Yes| P[Send SUCCESS + Device Info]
    P --> Q[Create Connection]
    Q --> R[Add to Connection Manager]
    R --> S[Bind Device to Connection]
    S --> T[Export Device via Driver]
    T --> U[Start Connection Threads]
    U --> D
    
    I -->|Other| V[Send NA]
    V --> K
    
    Z --> W[usbip_server_cleanup]
    W --> X[Connection Manager Cleanup]
    X --> Y[Transport Destroy]
    Y --> END[End]
```

## 3. 连接生命周期流程

```mermaid
flowchart TD
    subgraph Creation["Connection Creation"]
        A1[usbip_connection_create] --> A2[Allocate Connection Structure]
        A2 --> A3[Initialize State: CONN_STATE_INIT]
        A3 --> A4[Store Transport Context]
        A4 --> A5[Initialize State Lock]
        A5 --> A6[Return Connection]
    end

    subgraph Starting["Connection Starting"]
        B1[usbip_connection_start] --> B2{State == INIT?}
        B2 -->|No| B_ERR[Return Error]
        B2 -->|Yes| B3[Store Driver & BusID]
        B3 --> B4[Initialize URB Queue]
        B4 --> B5[Set running = 1]
        B5 --> B6[Create Processor Thread]
        B6 --> B7[Create RX Thread]
        B7 --> B8[Update State: CONN_STATE_ACTIVE]
        B8 --> B9[Return Success]
    end

    subgraph Stopping["Connection Stopping"]
        C1[usbip_connection_stop] --> C2{State == ACTIVE?}
        C2 -->|No| C_DONE[Return]
        C2 -->|Yes| C3[Set State: CONN_STATE_CLOSING]
        C3 --> C4[Set running = 0]
        C4 --> C5[Close URB Queue]
        C5 --> C6[Wake Waiting Threads]
        C6 --> C7[Join Processor Thread]
        C7 --> C8[Join RX Thread]
        C8 --> C9[Destroy URB Queue]
        C9 --> C10[Set State: CONN_STATE_CLOSED]
    end

    subgraph Destruction["Connection Destruction"]
        D1[usbip_connection_destroy] --> D2[Remove from Manager]
        D2 --> D3{State Active?}
        D3 -->|Yes| D4[Call usbip_connection_stop]
        D3 -->|No| D5[Destroy State Lock]
        D4 --> D5
        D5 --> D6[Close Transport Context]
        D6 --> D7[Free Connection Memory]
    end

    A6 --> B1
    B9 --> C1
    C10 --> D1
```

## 4. URB 处理流程（每连接）

```mermaid
flowchart TD
    subgraph RXThread["RX Thread (usbip_conn_rx_thread)"]
        R1[While running] --> R2[usbip_recv_header]
        R2 --> R3{Receive OK?}
        R3 -->|No| R_EXIT[Signal Stop]
        R3 -->|Yes| R4{Direction = OUT?}
        R4 -->|Yes| R5[Receive URB Data]
        R4 -->|No| R6[Data Len = 0]
        R5 --> R7[usbip_urb_queue_push]
        R6 --> R7
        R7 --> R8{Push OK?}
        R8 -->|No| R_EXIT
        R8 -->|Yes| R1
        R_EXIT --> R_CLOSE[Close URB Queue]
        R_CLOSE --> R_END[Thread Exit]
    end

    subgraph ProcessorThread["Processor Thread (usbip_conn_processor_thread)"]
        P1[While running] --> P2[usbip_urb_queue_pop]
        P2 --> P3{Pop OK?}
        P3 -->|No| P_EXIT[Thread Exit]
        P3 -->|Yes| P4[driver->handle_urb]
        P4 --> P5{Handle OK?}
        P5 -->|No| P_FREE[Free data_out]
        P5 -->|Yes| P6{Need Response?}
        P6 -->|Yes| P7[usbip_urb_send_reply]
        P6 -->|No| P8[Free data_out]
        P7 --> P8
        P_FREE --> P_EXIT
        P8 --> P1
    end

    subgraph URBQueue["URB Queue Operations"]
        Q1[Push] --> Q2{Queue Full?}
        Q2 -->|Yes| Q3[Wait on not_full]
        Q2 -->|No| Q4[Store URB in Slot]
        Q3 --> Q4
        Q4 --> Q5[Signal not_empty]
        
        Q6[Pop] --> Q7{Queue Empty?}
        Q7 -->|Yes| Q8[Wait on not_empty]
        Q7 -->|No| Q9[Retrieve URB from Slot]
        Q8 --> Q9
        Q9 --> Q10[Signal not_full]
        
        Q11[Close] --> Q12[Set closed = 1]
        Q12 --> Q13[Broadcast not_empty]
        Q12 --> Q14[Broadcast not_full]
    end
```

## 5. 设备管理流程

```mermaid
flowchart TD
    subgraph DriverRegistration["Driver Registration"]
        DR1[usbip_register_driver] --> DR2{Registry Full?}
        DR2 -->|Yes| DR_ERR[Return Error]
        DR2 -->|No| DR3{Already Registered?}
        DR3 -->|Yes| DR_ERR
        DR3 -->|No| DR4[Add to Registry]
        DR4 --> DR5[Call driver->init]
        DR5 --> DR6{Init OK?}
        DR6 -->|No| DR_ROLL[Remove from Registry]
        DR6 -->|Yes| DR_OK[Return Success]
        DR_ROLL --> DR_ERR
    end

    subgraph DeviceBinding["Device Binding"]
        DB1[usbip_bind_device] --> DB2[Check if Already Exported]
        DB2 --> DB3{Device Exists?}
        DB3 -->|Yes| DB4{State = EXPORTED?}
        DB4 -->|Yes| DB_ERR[Return -EBUSY]
        DB4 -->|No| DB5[Update Owner = conn]
        DB3 -->|No| DB6[Create New Entry]
        DB6 --> DB7[Set Owner = conn]
        DB7 --> DB8[Set State = EXPORTED]
        DB5 --> DB_OK[Return 0]
        DB8 --> DB_OK
    end

    subgraph DeviceUnbinding["Device Unbinding"]
        DU1[usbip_unbind_device] --> DU2[Find Device Entry]
        DU2 --> DU3{Found?}
        DU3 -->|No| DU_DONE[Return]
        DU3 -->|Yes| DU4[Set State = AVAILABLE]
        DU4 --> DU5[Set Owner = NULL]
        DU5 --> DU_DONE
    end

    subgraph DeviceEnumeration["Device Enumeration"]
        DE1[get_device_count] --> DE2[Iterate All Drivers]
        DE2 --> DE3{Device Busy?}
        DE3 -->|Yes| DE4[Skip]
        DE3 -->|No| DE5[Count++]
        
        DE6[get_device_by_index] --> DE7[Find Driver by Index]
        DE7 --> DE8{Device Busy?}
        DE8 -->|Yes| DE_ERR[Return -1]
        DE8 -->|No| DE_OK[Return Device Info]
    end
```

## 6. 多客户端并发架构

```mermaid
flowchart TB
    subgraph MainThread["Main Thread"]
        M1[Server Main Loop] --> M2[Accept Connection 1]
        M2 --> M3[Start Connection 1 Handler]
        M3 --> M4[Accept Connection 2]
        M4 --> M5[Start Connection 2 Handler]
        M5 --> M6[Accept Connection N...]
    end

    subgraph Connection1["Connection 1: Device A (2-1)"]
        C1_URB[URB Queue 1]
        C1_RX[RX Thread 1] --> C1_URB
        C1_PROC[Processor Thread 1] --> C1_URB
        C1_PROC --> C1_DRV[HID DAP Driver]
    end

    subgraph Connection2["Connection 2: Device B (2-2)"]
        C2_URB[URB Queue 2]
        C2_RX[RX Thread 2] --> C2_URB
        C2_PROC[Processor Thread 2] --> C2_URB
        C2_PROC --> C2_DRV[Bulk DAP Driver]
    end

    subgraph SharedState["Shared State (Protected by Locks)"]
        S1[Connection Manager List]
        S2[Device Registry]
    end

    M3 -.->|Add| S1
    M5 -.->|Add| S1
    C1_RX -.->|Bind| S2
    C2_RX -.->|Bind| S2
    
    style MainThread fill:#e1f5fe
    style Connection1 fill:#f3e5f5
    style Connection2 fill:#f3e5f5
    style SharedState fill:#fff3e0
```

## 7. 错误处理和清理流程

```mermaid
flowchart TD
    subgraph ErrorHandling["Error Handling"]
        E1[Import Request] --> E2[Create Connection]
        E2 --> E3{Create OK?}
        E3 -->|No| E_FAIL1[Close Transport]
        E3 -->|Yes| E4[Add to Manager]
        E4 --> E5{Add OK?}
        E5 -->|No| E_FAIL2[Destroy Connection]
        E5 -->|Yes| E6[Bind Device]
        E6 --> E7{Bind OK?}
        E7 -->|No| E_FAIL3[Remove from Manager]
        E7 -->|Yes| E8[Export Device]
        E8 --> E9{Export OK?}
        E9 -->|No| E_FAIL4[Unbind Device]
        E9 -->|Yes| E10[Start Connection]
        E10 --> E11{Start OK?}
        E11 -->|No| E_FAIL5[Unexport + Unbind]
        E11 -->|Yes| E_SUCCESS[Success]
        
        E_FAIL5 --> E_FAIL4
        E_FAIL4 --> E_FAIL3
        E_FAIL3 --> E_FAIL2
        E_FAIL2 --> E_FAIL1
    end

    subgraph Cleanup["Server Cleanup"]
        C1[usbip_server_stop] --> C2[Set running = 0]
        C2 --> C3[usbip_server_cleanup]
        C3 --> C4[usbip_conn_manager_cleanup]
        C4 --> C5[Iterate All Connections]
        C5 --> C6[Stop Each Connection]
        C6 --> C7[Destroy Each Connection]
        C7 --> C8[Destroy Manager Lock]
        C8 --> C9[transport_destroy]
        C9 --> C10[Unregister Drivers]
    end
```

## 8. 时序图：完整生命周期（导入 → 处理 → 断开 → 清理）

```mermaid
sequenceDiagram
    participant Client as USBIP Client
    participant Server as USBIP Server
    participant ConnMgr as Connection Manager
    participant URBQueue as URB Queue
    participant Driver as Device Driver

    Note over Client,Driver: ===== 阶段1: 连接建立 =====
    Client->>Server: OP_REQ_IMPORT (busid)
    Server->>Server: Find Device
    Server->>Server: Check Availability
    Server->>Client: OP_REP_IMPORT (OK + Device Info)
    
    Server->>ConnMgr: usbip_connection_create(ctx)
    ConnMgr-->>Server: Return Connection
    
    Server->>ConnMgr: usbip_conn_manager_add(conn)
    ConnMgr-->>Server: Return Success
    
    Server->>Server: usbip_bind_device(busid, conn)
    
    Server->>Driver: export_device(busid, conn)
    Driver-->>Server: Return Success
    
    Server->>ConnMgr: usbip_connection_start(conn, driver, busid)
    ConnMgr->>URBQueue: usbip_urb_queue_init()
    ConnMgr->>ConnMgr: Create Processor Thread
    ConnMgr->>ConnMgr: Create RX Thread
    ConnMgr-->>Server: Return Success
    
    Note over Client,Driver: ===== 阶段2: URB 处理 =====
    
    par RX Thread
        loop While Running
            Client->>Server: URB Request
            Server->>URBQueue: usbip_urb_queue_push()
        end
    and Processor Thread
        loop While Running
            URBQueue->>Driver: usbip_urb_queue_pop()
            Driver->>Driver: handle_urb()
            Driver->>Client: URB Response
        end
    end
    
    Note over Client,Driver: ===== 阶段3: 连接断开 / 清理 =====
    
    alt 客户端断开或网络错误
        Client-xServer: Connection Lost
        Server->>Server: RX Thread detects error
        Server->>ConnMgr: usbip_connection_stop(conn)
    else 服务器主动关闭
        Server->>Server: usbip_server_stop()
        Server->>ConnMgr: usbip_conn_manager_cleanup()
    end
    
    ConnMgr->>URBQueue: usbip_urb_queue_close()
    URBQueue-->>ConnMgr: Wake all waiting threads
    
    ConnMgr->>ConnMgr: Join Processor Thread
    ConnMgr->>ConnMgr: Join RX Thread
    
    ConnMgr->>URBQueue: usbip_urb_queue_destroy()
    ConnMgr->>Server: usbip_connection_destroy(conn)
    Server->>ConnMgr: usbip_conn_manager_remove(conn)
    Server->>Server: usbip_unbind_device(busid)
    Server->>Driver: unexport_device(busid)
    Driver-->>Server: Device Available Again
```

## 9. 关键数据结构关系

```mermaid
classDiagram
    class usbip_connection {
        +usbip_connection* next
        +usbip_connection* prev
        +usbip_conn_ctx* transport_ctx
        +usbip_device_driver* driver
        +char busid[SYSFS_BUS_ID_SIZE]
        +usbip_conn_state state
        +osal_mutex state_lock
        +usbip_conn_urb_queue urb_queue
        +osal_thread rx_thread
        +osal_thread processor_thread
        +volatile int running
        +int rx_thread_started
        +int processor_started
    }

    class usbip_conn_urb_queue {
        +void* priv
    }

    class urb_queue {
        +urb_slot slots[USBIP_URB_QUEUE_SIZE]
        +int head
        +int tail
        +osal_mutex lock
        +osal_cond not_empty
        +osal_cond not_full
        +int closed
    }

    class usbip_device_driver {
        +const char* name
        +get_device_count()
        +get_device_by_index()
        +get_device()
        +export_device()
        +unexport_device()
        +handle_urb()
        +init()
        +cleanup()
    }

    class device_registry_entry {
        +char busid[SYSFS_BUS_ID_SIZE]
        +device_state state
        +usbip_connection* owner
    }

    usbip_connection "1" --> "1" usbip_conn_urb_queue : has
    usbip_conn_urb_queue "1" --> "1" urb_queue : priv points to
    usbip_connection "*" --> "1" usbip_device_driver : uses
    device_registry_entry "*" --> "0..1" usbip_connection : owned by
```

## 10. 线程模型

```mermaid
flowchart TB
    subgraph Main["Main Thread (usbip_server_run)"]
        direction TB
        M1["Initialize"] --> M2["transport_accept()"]
        M2 --> M3{Connection?}
        M3 -->|No| M2
        M3 -->|Yes| M4{"Operation Type"}
        M4 -->|DEVLIST| M5["Send Device List"]
        M4 -->|IMPORT| M6["Create Connection"]
        M6 --> M7["Start RX + Processor Threads"]
        M5 --> M8["Close Connection"]
        M7 --> M2
        M8 --> M2
    end

    subgraph Conn1["Connection 1: Device A"]
        direction TB
        subgraph C1_Queue["URB Queue 1 (Isolated)"]
            Q1["Ring Buffer<br/>Slots[8]"]
        end
        
        subgraph C1_RX_Thread["RX Thread (usbip_conn_rx_thread)"]
            R1["transport_recv()"]
            R2["usbip_urb_queue_push()"]
            R1 --> R2
        end
        
        subgraph C1_PROC_Thread["Processor Thread (usbip_conn_processor_thread)"]
            P1["usbip_urb_queue_pop()"]
            P2["driver->handle_urb()"]
            P3["transport_send()"]
            P1 --> P2 --> P3
        end
        
        R2 -.->|Push URB| Q1
        Q1 -.->|Pop URB| P1
    end

    subgraph Conn2["Connection 2: Device B"]
        direction TB
        subgraph C2_Queue["URB Queue 2 (Isolated)"]
            Q2["Ring Buffer<br/>Slots[8]"]
        end
        
        subgraph C2_RX_Thread["RX Thread"]
            R3["transport_recv()"]
            R4["usbip_urb_queue_push()"]
            R3 --> R4
        end
        
        subgraph C2_PROC_Thread["Processor Thread"]
            P4["usbip_urb_queue_pop()"]
            P5["driver->handle_urb()"]
            P6["transport_send()"]
            P4 --> P5 --> P6
        end
        
        R4 -.->|Push URB| Q2
        Q2 -.->|Pop URB| P4
    end

    M7 -.->|Spawns| C1_RX_Thread
    M7 -.->|Spawns| C1_PROC_Thread
    M7 -.->|Spawns| C2_RX_Thread
    M7 -.->|Spawns| C2_PROC_Thread

    style Main fill:#e3f2fd
    style Conn1 fill:#f3e5f5
    style Conn2 fill:#f3e5f5
    style C1_Queue fill:#fff9c4
    style C2_Queue fill:#fff9c4
```

## 11. 系统生命周期（启动 → 运行 → 关闭）

```mermaid
flowchart TB
    subgraph Startup["▶️ 系统启动阶段"]
        direction TB
        ST1["程序启动"] --> ST2["Driver Auto-Register<br/>__attribute__((constructor))"]
        ST2 --> ST3["hid_dap_driver_register()"]
        ST2 --> ST4["bulk_dap_driver_register()"]
        ST3 --> ST5["usbip_server_init()"]
        ST4 --> ST5
        ST5 --> ST6["usbip_conn_manager_init()<br/>初始化连接管理器"]
        ST6 --> ST7["transport_listen()<br/>启动监听端口"]
        ST7 --> ST8["Server Ready ✓"]
    end

    subgraph Runtime["⏯️ 运行阶段"]
        direction TB
        RT1["usbip_server_run()"] --> RT2["transport_accept()"]
        RT2 --> RT3{"Import Request?"}
        RT3 -->|Yes| RT4["创建 Connection"]
        RT3 -->|No| RT5["处理其他请求"]
        RT5 --> RT1
        RT4 --> RT6["启动 RX + Processor 线程"]
        RT6 --> RT1
        
        subgraph Conn1_Runtime["Connection 1 运行中"]
            C1R1["RX: 接收 URB"] --> C1R2["Queue: Push"]
            C1R3["Queue: Pop"] --> C1R4["Processor: 处理 URB"]
            C1R5["Processor: 发送响应"]
        end
        
        subgraph Conn2_Runtime["Connection 2 运行中"]
            C2R1["RX: 接收 URB"] --> C2R2["Queue: Push"]
            C2R3["Queue: Pop"] --> C2R4["Processor: 处理 URB"]
            C2R5["Processor: 发送响应"]
        end
        
        RT6 -.->|Activates| Conn1_Runtime
        RT6 -.->|Activates| Conn2_Runtime
    end

    subgraph Shutdown["⏹️ 系统关闭阶段"]
        direction TB
        SH1["usbip_server_stop()<br/>设置 running = 0"] --> SH2["usbip_server_cleanup()"]
        SH2 --> SH3["usbip_conn_manager_cleanup()"]
        SH3 --> SH4["遍历所有连接"]
        SH4 --> SH5["usbip_connection_stop()"]
        SH5 --> SH6["关闭 URB Queue<br/>唤醒等待线程"]
        SH6 --> SH7["Join RX Thread"]
        SH7 --> SH8["Join Processor Thread"]
        SH8 --> SH9["usbip_connection_destroy()"]
        SH9 --> SH10["transport_destroy()"]
        SH10 --> SH11["Cleanup Complete ✓"]
    end

    ST8 --> RT1
    
    %% 进入关闭阶段的触发条件
    RT1 -.->|1. SIGINT/SIGTERM<br/>2. usbip_server_stop() called| SH1
    Conn1_Runtime -.->|RX/Processor Thread Exit| SH3
    Conn2_Runtime -.->|RX/Processor Thread Exit| SH3
    
    style Startup fill:#e8f5e9
    style Runtime fill:#e3f2fd
    style Shutdown fill:#ffebee
    style Conn1_Runtime fill:#f3e5f5
    style Conn2_Runtime fill:#f3e5f5
```

---

## 清理阶段（阶段5）触发条件总结

清理阶段（CleanupPhase）在以下三种情况下被触发：

```mermaid
flowchart LR
    T1["1. 客户端断开<br/>Connection Error"] --> CLEANUP["CleanupPhase<br/>usbip_connection_stop()"]
    T2["2. 服务器关闭<br/>SIGINT/SIGTERM"] --> CLEANUP
    T3["3. 导入失败<br/>Rollback"] --> CLEANUP
    
    style T1 fill:#ffebee
    style T2 fill:#ffebee
    style T3 fill:#ffebee
    style CLEANUP fill:#ffcdd2
```

### 触发条件详情

| 触发条件 | 入口函数 | 触发点 | 清理范围 |
|---------|---------|--------|---------|
| **客户端断开** | `usbip_connection_stop()` | RX/Processor 线程检测到连接错误 | 单个连接 |
| **服务器关闭** | `usbip_conn_manager_cleanup()` | `usbip_server_stop()` 被调用 | 所有连接 |
| **导入失败回滚** | `usbip_connection_destroy()` | `export_device()` 或 `connection_start()` 失败 | 部分创建的连接 |

### 清理流程执行顺序

```
1. usbip_connection_stop()
   └── 设置 running = 0
   └── 关闭 URB Queue (唤醒等待线程)
   └── Join Processor Thread
   └── Join RX Thread
   └── 销毁 URB Queue
   └── 状态 = CONN_STATE_CLOSED

2. usbip_connection_destroy()
   └── 从连接管理器移除
   └── 关闭 Transport Context
   └── 释放内存

3. usbip_unbind_device()
   └── 解绑设备与连接
   └── 设备状态 = AVAILABLE

4. driver->unexport_device()
   └── 驱动清理
   └── 设备可再次被导入
```

---

**说明：**

1. **多客户端支持**：每个设备连接有独立的 RX 和 Processor 线程，互不干扰
2. **线程安全**：连接管理器列表和设备注册表使用互斥锁保护
3. **优雅关闭**：通过 `running` 标志和队列关闭机制确保线程安全退出
4. **资源隔离**：每连接独立的 URB 队列防止一个客户端影响其他客户端
5. **正确的初始化顺序**：`usbip_conn_manager_init()` 在 `transport_listen()` 之前调用
6. **完整的清理路径**：所有创建阶段的资源都有对应的释放路径，确保无内存泄漏
