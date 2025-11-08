# libice 架构文档

## 1. 概述

libice 是一个实现 ICE (Interactive Connectivity Establishment) 协议的 C 语言库，用于建立穿越 NAT 的对等连接。它集成了 STUN 和 TURN 协议，为实时通信应用提供可靠的 NAT 穿透解决方案。

### 1.1 相关协议标准

- **RFC 5245** - Interactive Connectivity Establishment (ICE)
- **RFC 8445** - ICE: A Protocol for Network Address Translator (NAT) Traversal (更新版)
- **RFC 5389** - Session Traversal Utilities for NAT (STUN)
- **RFC 3489** - STUN (经典版本)
- **RFC 5766** - Traversal Using Relays around NAT (TURN)
- **RFC 6156** - TURN Extension for IPv6
- **RFC 5245** Section 4.1.2 - 候选地址优先级计算

### 1.2 主要功能

- ICE 候选地址收集（Host、Server Reflexive、Relayed）
- STUN Binding 请求/响应处理
- TURN 中继地址分配和管理
- 连接性检查（Connectivity Check）
- 候选地址对提名（Nomination）
- 数据传输通道管理
- 短期/长期凭证认证
- TCP/UDP 传输支持

## 2. 核心概念

### 2.1 ICE (Interactive Connectivity Establishment)

ICE 是一个用于建立对等连接的协议框架，通过以下步骤实现 NAT 穿透：

1. **候选地址收集（Gathering）**：收集所有可能的网络地址
   - Host Candidate：本地网络接口地址
   - Server Reflexive Candidate：STUN 服务器观察到的公网地址
   - Relayed Candidate：TURN 服务器分配的中继地址

2. **候选地址交换**：通过信令通道（如 SIP）交换候选地址

3. **连接性检查（Connectivity Check）**：
   - 对所有候选地址对进行 STUN Binding 请求测试
   - 验证端到端连接可达性
   - 计算候选地址对的优先级

4. **提名（Nomination）**：
   - Controlling 角色选择最佳候选地址对
   - 发送带 USE-CANDIDATE 属性的 STUN 请求
   - Regular Nomination vs Aggressive Nomination

### 2.2 ICE 角色

- **Controlling**：主动发起方（通常是呼叫方）
  - 负责最终选择和提名候选地址对
  - 在 STUN 请求中包含 ICE-CONTROLLING 属性

- **Controlled**：被动响应方（通常是被叫方）
  - 响应连接性检查请求
  - 在 STUN 响应中包含 ICE-CONTROLLED 属性

### 2.3 候选地址类型

```c
enum ice_candidate_type_t
{
    ICE_CANDIDATE_HOST = 0,              // 本地主机地址
    ICE_CANDIDATE_SERVER_REFLEXIVE = 1,  // STUN 服务器反射地址
    ICE_CANDIDATE_PEER_REFLEXIVE = 2,    // 对等反射地址（连接性检查中发现）
    ICE_CANDIDATE_RELAYED = 3,           // TURN 中继地址
};
```

**优先级计算（RFC 5245 4.1.2）**：
```
priority = (2^24) * (type preference) +
           (2^8) * (local preference) +
           (2^0) * (256 - component ID)

type preference:
  - host: 126
  - peer reflexive: 110
  - server reflexive: 100
  - relayed: 0
```

### 2.4 STUN (Session Traversal Utilities for NAT)

STUN 是一个轻量级的 C/S 协议，用于：
- 发现 NAT 设备存在和类型
- 获取公网 IP 地址和端口
- 检查端到端连接可达性
- 保持 NAT 绑定活跃（Keep-alive）

**STUN 消息类型**：
- Binding Request/Response：绑定请求/响应
- Indication：单向指示消息（无响应）

**STUN 属性**：
- MAPPED-ADDRESS：映射的公网地址
- XOR-MAPPED-ADDRESS：XOR 加密的映射地址
- USERNAME：用户名（认证）
- MESSAGE-INTEGRITY：消息完整性校验（HMAC-SHA1）
- PRIORITY：ICE 优先级
- USE-CANDIDATE：ICE 提名标志
- ICE-CONTROLLING/ICE-CONTROLLED：ICE 角色标识

### 2.5 TURN (Traversal Using Relays around NAT)

TURN 是 STUN 的扩展，提供中继服务用于最保守的 NAT 穿透场景：

**TURN 操作**：
1. **Allocate**：在 TURN 服务器上分配中继地址
2. **Refresh**：刷新分配的生命周期（默认 600 秒）
3. **CreatePermission**：为对等端创建发送权限
4. **ChannelBind**：绑定通道号，减少开销
5. **Send/Data**：发送/接收数据指示消息

**通道绑定优化**：
- 普通 TURN 数据：36 字节头部开销
- 通道绑定：4 字节头部开销
- 通道号范围：0x4000 - 0x7FFF

## 3. 数据结构

### 3.1 ICE Agent

```c
struct ice_agent_t
{
    stun_agent_t* stun;                  // STUN 代理（内部使用）
    struct list_head streams;            // ICE 流列表
    enum ice_nomination_t nomination;    // 提名模式
    uint64_t tiebreaking;                // 角色冲突解决
    int controlling;                     // 1=controlling, 0=controlled
    int icelite;                         // ICE-Lite 模式
    struct sockaddr_storage saddr;       // 本地地址
    struct stun_credential_t auth;       // STUN 认证凭证
};
```

### 3.2 ICE Candidate（候选地址）

```c
struct ice_candidate_t
{
    enum ice_candidate_type_t type;      // 候选地址类型
    struct sockaddr_storage addr;        // 候选地址
    struct sockaddr_storage host;        // 基础地址（base）
    uint8_t stream;                      // 流标识
    uint16_t component;                  // 组件 ID（1=RTP, 2=RTCP）
    uint32_t priority;                   // 优先级 [1, 2^31-1]
    char foundation[33];                 // 基础标识（用于配对）
    int protocol;                        // 传输协议（UDP/TCP）
};
```

**优先级计算函数**：
```c
static inline void ice_candidate_priority(struct ice_candidate_t* c)
{
    static const uint8_t s_priority[] = { 126 /*host*/, 100 /*srflx*/, 110 /*prflx*/, 0 /*relay*/ };
    uint16_t v = (1 << 10) * c->host.ss_family;  // IPv6 > IPv4
    c->priority = (1 << 24) * s_priority[c->type] + (1 << 8) * v + (256 - c->component);
}
```

### 3.3 ICE Candidate Pair（候选地址对）

```c
enum ice_candidate_pair_state_t
{
    ICE_CANDIDATE_PAIR_FROZEN = 0,    // 冻结（等待检查）
    ICE_CANDIDATE_PAIR_WAITING,       // 等待（队列中）
    ICE_CANDIDATE_PAIR_INPROGRESS,    // 进行中
    ICE_CANDIDATE_PAIR_SUCCEEDED,     // 成功
    ICE_CANDIDATE_PAIR_FAILED,        // 失败
};

struct ice_candidate_pair_t
{
    struct ice_candidate_t local;     // 本地候选地址
    struct ice_candidate_t remote;    // 远端候选地址
    enum ice_candidate_pair_state_t state;  // 状态
    int nominated;                    // 是否已提名
    uint64_t priority;                // 配对优先级
    char foundation[66];              // 基础标识组合
    struct ice_stream_t* stream;      // 所属流
};
```

**配对优先级计算**：
```c
// Controlling 角色
pair_priority = 2^32 * MIN(G,D) + 2 * MAX(G,D) + (G>D?1:0)

// G = local candidate priority
// D = remote candidate priority
```

### 3.4 ICE Stream（ICE 流）

```c
struct ice_stream_t
{
    struct list_head link;              // 链表节点
    int stream;                         // 流 ID
    int status;                         // 流状态
    ice_candidates_t locals;            // 本地候选地址列表
    ice_candidates_t remotes;           // 远端候选地址列表
    struct ice_checklist_t* checklist;  // 连接性检查列表
    struct stun_credential_t rauth;     // 远端认证凭证
    struct ice_candidate_pair_t components[ICE_COMPONENT_MAX];  // 已提名的组件
    int ncomponent;                     // 组件数量
};
```

### 3.5 STUN Message（STUN 消息）

```c
struct stun_header_t
{
    uint16_t msgtype;      // 消息类型
    uint16_t length;       // 消息体长度
    uint32_t cookie;       // Magic Cookie (0x2112A442)
    uint8_t tid[12];       // 事务 ID（Transaction ID）
};

struct stun_message_t
{
    struct stun_header_t header;
    struct stun_attr_t attrs[STUN_ATTR_N];  // 属性数组
    int nattrs;                              // 属性数量
};
```

### 3.6 STUN Credential（STUN 凭证）

```c
enum stun_credential_mechanism_t
{
    STUN_CREDENTIAL_NONE = 0,
    STUN_CREDENTIAL_SHORT_TERM = 1,  // 短期凭证
    STUN_CREDENTIAL_LONG_TERM = 2,   // 长期凭证
};

struct stun_credential_t
{
    int credential;                        // 凭证类型
    char usr[STUN_LIMIT_USERNAME_MAX];     // 用户名
    char pwd[STUN_LIMIT_USERNAME_MAX];     // 密码
    char realm[128];                       // 域（长期凭证）
    char nonce[128];                       // 随机数（长期凭证）
};
```

## 4. API 接口

### 4.1 ICE Agent API

#### 4.1.1 代理创建和销毁

```c
/**
 * @brief 创建 ICE 代理
 * @param controlling 1=controlling 角色, 0=controlled 角色
 * @param handler 事件处理回调
 * @param param 用户参数
 * @return ICE 代理句柄，失败返回 NULL
 */
struct ice_agent_t* ice_agent_create(int controlling,
                                      struct ice_agent_handler_t* handler,
                                      void* param);

/**
 * @brief 销毁 ICE 代理
 * @param ice ICE 代理句柄
 */
int ice_agent_destroy(struct ice_agent_t** ice);
```

#### 4.1.2 候选地址管理

```c
/**
 * @brief 添加本地候选地址
 * @param ice ICE 代理句柄
 * @param stream 流 ID
 * @param component 组件 ID (1=RTP, 2=RTCP)
 * @param protocol 协议类型 (IPPROTO_UDP/IPPROTO_TCP)
 * @param candidate 候选地址
 * @return 0=成功, <0=失败
 */
int ice_agent_add_local_candidate(struct ice_agent_t* ice,
                                   uint8_t stream, uint16_t component,
                                   int protocol,
                                   const struct sockaddr_storage* candidate);

/**
 * @brief 添加远端候选地址
 * @param ice ICE 代理句柄
 * @param stream 流 ID
 * @param component 组件 ID
 * @param protocol 协议类型
 * @param foundation 基础标识
 * @param priority 优先级
 * @param candidate 候选地址
 * @return 0=成功, <0=失败
 */
int ice_agent_add_remote_candidate(struct ice_agent_t* ice,
                                    uint8_t stream, uint16_t component,
                                    int protocol, const char* foundation,
                                    uint32_t priority,
                                    const struct sockaddr_storage* candidate);
```

#### 4.1.3 候选地址收集

```c
/**
 * @brief 收集候选地址（通过 STUN/TURN）
 * @param ice ICE 代理句柄
 * @param stream 流 ID
 * @param component 组件 ID
 * @param protocol 协议类型
 * @param host 本地地址
 * @param stun STUN 服务器地址
 * @param turn TURN 服务器地址（可选）
 * @param usr TURN 用户名（可选）
 * @param pwd TURN 密码（可选）
 * @return 0=成功, <0=失败
 */
int ice_agent_gather(struct ice_agent_t* ice,
                     uint8_t stream, uint16_t component,
                     int protocol,
                     const struct sockaddr_storage* host,
                     const struct sockaddr_storage* stun,
                     const struct sockaddr_storage* turn,
                     const char* usr, const char* pwd);
```

#### 4.1.4 连接性检查

```c
/**
 * @brief 启动连接性检查
 * @param ice ICE 代理句柄
 * @param stream 流 ID
 * @param usr 本地用户名片段
 * @param pwd 本地密码片段
 * @param rusr 远端用户名片段
 * @param rpwd 远端密码片段
 * @return 0=成功, <0=失败
 */
int ice_agent_start(struct ice_agent_t* ice, uint8_t stream,
                    const char* usr, const char* pwd,
                    const char* rusr, const char* rpwd);
```

#### 4.1.5 数据传输

```c
/**
 * @brief 发送数据
 * @param ice ICE 代理句柄
 * @param stream 流 ID
 * @param component 组件 ID
 * @param data 数据缓冲区
 * @param bytes 数据长度
 * @return 发送的字节数，<0=失败
 */
int ice_agent_send(struct ice_agent_t* ice,
                   uint8_t stream, uint16_t component,
                   const void* data, int bytes);

/**
 * @brief 接收网络数据（调用此函数处理收到的网络包）
 * @param ice ICE 代理句柄
 * @param protocol 协议类型
 * @param local 本地地址
 * @param remote 远端地址
 * @param data 数据缓冲区
 * @param bytes 数据长度
 * @return 0=成功, <0=失败
 */
int ice_agent_input(struct ice_agent_t* ice,
                    int protocol,
                    const struct sockaddr* local,
                    const struct sockaddr* remote,
                    const void* data, int bytes);
```

#### 4.1.6 事件回调

```c
struct ice_agent_handler_t
{
    /**
     * @brief 发送数据回调（由应用层实现底层发送）
     * @param param 用户参数
     * @param protocol 协议类型
     * @param local 本地地址
     * @param remote 远端地址
     * @param data 数据缓冲区
     * @param bytes 数据长度
     * @return 发送的字节数
     */
    int (*send)(void* param, int protocol,
                const struct sockaddr* local,
                const struct sockaddr* remote,
                const void* data, int bytes);

    /**
     * @brief 接收到应用数据（非 STUN/TURN 控制消息）
     * @param param 用户参数
     * @param stream 流 ID
     * @param component 组件 ID
     * @param data 数据缓冲区
     * @param bytes 数据长度
     */
    void (*ondata)(void* param, uint8_t stream, uint16_t component,
                   const void* data, int bytes);

    /**
     * @brief 候选地址收集完成
     * @param param 用户参数
     * @param code 0=成功, <0=失败
     */
    void (*ongather)(void* param, int code);

    /**
     * @brief 连接性检查完成（ICE 建立成功）
     * @param param 用户参数
     * @param flags 成功的组件位掩码
     * @param mask 所有组件位掩码
     */
    void (*onconnected)(void* param, uint64_t flags, uint64_t mask);
};
```

### 4.2 STUN Agent API

#### 4.2.1 STUN 代理创建

```c
/**
 * @brief 创建 STUN 代理
 * @param handler 事件处理回调
 * @param param 用户参数
 * @return STUN 代理句柄
 */
struct stun_agent_t* stun_agent_create(int rfc,
                                        struct stun_agent_handler_t* handler,
                                        void* param);

/**
 * @brief 销毁 STUN 代理
 */
int stun_agent_destroy(struct stun_agent_t** stun);
```

#### 4.2.2 STUN Binding 请求

```c
/**
 * @brief 发送 STUN Binding 请求
 * @param stun STUN 代理句柄
 * @param protocol 协议类型
 * @param local 本地地址
 * @param remote STUN 服务器地址
 * @param priority ICE 优先级（用于 ICE）
 * @param ice_controlling ICE controlling 角色标识
 * @param ice_controlled ICE controlled 角色标识
 * @param use_candidate 是否使用候选标志
 * @param credential STUN 凭证
 * @param id 事务 ID [out]
 * @return 0=成功, <0=失败
 */
int stun_agent_bind(struct stun_agent_t* stun,
                    int protocol,
                    const struct sockaddr* local,
                    const struct sockaddr* remote,
                    uint32_t priority,
                    uint64_t ice_controlling, uint64_t ice_controlled,
                    int use_candidate,
                    const struct stun_credential_t* credential,
                    void** id);
```

#### 4.2.3 TURN 操作

```c
/**
 * @brief 分配 TURN 中继地址
 * @param stun STUN 代理句柄
 * @param protocol 协议类型
 * @param local 本地地址
 * @param turn TURN 服务器地址
 * @param lifetime 分配生命周期（秒）
 * @param credential TURN 凭证
 * @param id 事务 ID [out]
 * @return 0=成功, <0=失败
 */
int turn_agent_allocate(struct stun_agent_t* stun,
                        int protocol,
                        const struct sockaddr* local,
                        const struct sockaddr* turn,
                        int lifetime,
                        const struct stun_credential_t* credential,
                        void** id);

/**
 * @brief 刷新 TURN 分配
 * @param stun STUN 代理句柄
 * @param protocol 协议类型
 * @param local 本地地址
 * @param turn TURN 服务器地址
 * @param lifetime 新的生命周期（秒）
 * @param credential TURN 凭证
 * @param id 事务 ID [out]
 * @return 0=成功, <0=失败
 */
int turn_agent_refresh(struct stun_agent_t* stun,
                       int protocol,
                       const struct sockaddr* local,
                       const struct sockaddr* turn,
                       int lifetime,
                       const struct stun_credential_t* credential,
                       void** id);

/**
 * @brief 创建 TURN 权限
 * @param stun STUN 代理句柄
 * @param protocol 协议类型
 * @param local 本地地址
 * @param turn TURN 服务器地址
 * @param peer 对等端地址
 * @param credential TURN 凭证
 * @param id 事务 ID [out]
 * @return 0=成功, <0=失败
 */
int turn_agent_create_permission(struct stun_agent_t* stun,
                                  int protocol,
                                  const struct sockaddr* local,
                                  const struct sockaddr* turn,
                                  const struct sockaddr* peer,
                                  const struct stun_credential_t* credential,
                                  void** id);

/**
 * @brief 绑定 TURN 通道
 * @param stun STUN 代理句柄
 * @param protocol 协议类型
 * @param local 本地地址
 * @param turn TURN 服务器地址
 * @param peer 对等端地址
 * @param channel 通道号 [0x4000, 0x7FFF]
 * @param credential TURN 凭证
 * @param id 事务 ID [out]
 * @return 0=成功, <0=失败
 */
int turn_agent_channel_bind(struct stun_agent_t* stun,
                             int protocol,
                             const struct sockaddr* local,
                             const struct sockaddr* turn,
                             const struct sockaddr* peer, uint16_t channel,
                             const struct stun_credential_t* credential,
                             void** id);
```

### 4.3 STUN Message API

#### 4.3.1 消息构造

```c
/**
 * @brief 添加 XOR-MAPPED-ADDRESS 属性
 */
int stun_attr_add_address(struct stun_message_t* msg, uint16_t type,
                          const struct sockaddr* addr);

/**
 * @brief 添加字符串属性
 */
int stun_attr_add_string(struct stun_message_t* msg, uint16_t type,
                         const char* v, int bytes);

/**
 * @brief 添加 32 位整数属性
 */
int stun_attr_add_uint32(struct stun_message_t* msg, uint16_t type, uint32_t v);

/**
 * @brief 添加 64 位整数属性
 */
int stun_attr_add_uint64(struct stun_message_t* msg, uint16_t type, uint64_t v);

/**
 * @brief 添加 MESSAGE-INTEGRITY 属性
 */
int stun_message_add_credentials(struct stun_message_t* msg,
                                  const struct stun_credential_t* auth);

/**
 * @brief 添加 FINGERPRINT 属性
 */
int stun_message_add_fingerprint(struct stun_message_t* msg);
```

#### 4.3.2 消息解析

```c
/**
 * @brief 解析 STUN 消息
 */
int stun_message_read(struct stun_message_t* msg, const uint8_t* data, int bytes);

/**
 * @brief 获取属性值
 */
const struct stun_attr_t* stun_message_attr_find(const struct stun_message_t* msg,
                                                  uint16_t type);

/**
 * @brief 获取地址属性
 */
int stun_attr_address(const struct stun_attr_t* attr,
                      struct sockaddr_storage* addr);

/**
 * @brief 获取字符串属性
 */
const uint8_t* stun_attr_string(const struct stun_attr_t* attr, int* bytes);

/**
 * @brief 获取整数属性
 */
uint32_t stun_attr_uint32(const struct stun_attr_t* attr);
uint64_t stun_attr_uint64(const struct stun_attr_t* attr);
```

#### 4.3.3 消息验证

```c
/**
 * @brief 验证 MESSAGE-INTEGRITY
 */
int stun_message_check_credential(const uint8_t* data, int bytes,
                                   const struct stun_credential_t* auth);

/**
 * @brief 验证 FINGERPRINT
 */
int stun_message_check_fingerprint(const uint8_t* data, int bytes);
```

## 5. 主要流程

### 5.1 ICE 完整流程

```
1. 创建 ICE Agent
   ├─ ice_agent_create(controlling, handler, param)
   └─ 设置角色：controlling/controlled

2. 收集本地候选地址
   ├─ ice_agent_add_local_candidate() - 添加 Host 候选
   └─ ice_agent_gather() - 通过 STUN/TURN 收集 Reflexive/Relayed 候选
        ├─ STUN Binding → 获取 Server Reflexive 地址
        └─ TURN Allocate → 获取 Relayed 地址

3. 候选地址交换（通过 SIP/WebRTC 信令）
   ├─ 本地候选 → 发送给对端（SDP）
   └─ 远端候选 ← 从对端接收（SDP）
        └─ ice_agent_add_remote_candidate()

4. 启动连接性检查
   ├─ ice_agent_start(stream, usr, pwd, rusr, rpwd)
   └─ 对所有候选地址对执行 STUN Binding 检查
        ├─ Controlling: 发送带 ICE-CONTROLLING 的 Binding Request
        ├─ Controlled: 发送带 ICE-CONTROLLED 的 Binding Response
        └─ 根据响应更新候选地址对状态

5. 提名候选地址对
   ├─ Controlling 角色选择最佳候选地址对
   └─ 发送带 USE-CANDIDATE 的 Binding Request
        └─ Controlled 响应后，该对被提名

6. 连接建立成功
   └─ onconnected(param, flags, mask) 回调

7. 数据传输
   ├─ ice_agent_send() - 发送数据
   ├─ ice_agent_input() - 接收网络包
   └─ ondata() - 应用数据回调
```

### 5.2 候选地址收集流程

```c
// 伪代码示例
ice_agent_t* ice = ice_agent_create(1, &handler, NULL);

// 1. 添加本地主机候选地址
struct sockaddr_storage host_addr;
// ... 初始化 host_addr ...
ice_agent_add_local_candidate(ice, 1, 1, IPPROTO_UDP, &host_addr);

// 2. 通过 STUN 收集 Server Reflexive 候选地址
struct sockaddr_storage stun_server;
// ... 初始化 STUN 服务器地址 ...
ice_agent_gather(ice, 1, 1, IPPROTO_UDP, &host_addr, &stun_server, NULL, NULL, NULL);

// 3. 通过 TURN 收集 Relayed 候选地址（可选）
struct sockaddr_storage turn_server;
// ... 初始化 TURN 服务器地址 ...
ice_agent_gather(ice, 1, 1, IPPROTO_UDP, &host_addr, NULL,
                 &turn_server, "username", "password");

// 4. 等待 ongather() 回调
void ongather_callback(void* param, int code)
{
    if (code == 0) {
        // 收集成功，可以获取候选地址列表并通过信令交换
        // 将本地候选地址列表发送给对端
    }
}
```

### 5.3 连接性检查流程

```
Controlling Agent                          Controlled Agent
     |                                           |
     |  1. STUN Binding Request                  |
     |      + PRIORITY                           |
     |      + ICE-CONTROLLING                    |
     |      + USERNAME (local:remote)            |
     |      + MESSAGE-INTEGRITY                  |
     |------------------------------------------>|
     |                                           |
     |                                           |  2. 验证请求
     |                                           |     检查凭证
     |                                           |     触发回复
     |                                           |
     |  3. STUN Binding Response                 |
     |      + XOR-MAPPED-ADDRESS                 |
     |      + MESSAGE-INTEGRITY                  |
     |<------------------------------------------|
     |                                           |
     |  4. 候选地址对状态 → SUCCEEDED            |
     |                                           |
     |  5. 提名候选地址对                         |
     |     STUN Binding Request                  |
     |      + USE-CANDIDATE                      |
     |------------------------------------------>|
     |                                           |
     |  6. STUN Binding Response                 |
     |<------------------------------------------|
     |                                           |
     |  7. onconnected() 回调                    |  8. onconnected() 回调
     |                                           |
```

### 5.4 TURN 中继流程

```
Client                          TURN Server                    Peer
  |                                  |                           |
  | 1. Allocate Request              |                           |
  |    + REQUESTED-TRANSPORT (UDP)   |                           |
  |    + USERNAME, MESSAGE-INTEGRITY |                           |
  |--------------------------------->|                           |
  |                                  |                           |
  | 2. Allocate Success Response     |                           |
  |    + XOR-RELAYED-ADDRESS         |                           |
  |    + LIFETIME (600s)             |                           |
  |<---------------------------------|                           |
  |                                  |                           |
  | 3. CreatePermission Request      |                           |
  |    + XOR-PEER-ADDRESS (Peer IP)  |                           |
  |--------------------------------->|                           |
  |                                  |                           |
  | 4. CreatePermission Response     |                           |
  |<---------------------------------|                           |
  |                                  |                           |
  | 5. ChannelBind Request           |                           |
  |    + XOR-PEER-ADDRESS            |                           |
  |    + CHANNEL-NUMBER (0x4000)     |                           |
  |--------------------------------->|                           |
  |                                  |                           |
  | 6. ChannelBind Response          |                           |
  |<---------------------------------|                           |
  |                                  |                           |
  | 7. ChannelData (channel=0x4000)  |                           |
  |--------------------------------->| 8. Forward to Peer        |
  |                                  |-------------------------->|
  |                                  |                           |
  | 10. ChannelData from Peer        | 9. Data from Peer         |
  |<---------------------------------|<--------------------------|
  |                                  |                           |
```

### 5.5 定时器和保活机制

#### 5.5.1 连接性检查定时器

- **RTO (Retransmission Timeout)**: 500ms
- **最大重传次数**: 7 次
- **总超时时间**: 39.5 秒（500 + 1000 + 2000 + ... + 32000 ms）
- **Ta 定时器**: 50ms（检查对间隔）

#### 5.5.2 TURN 刷新定时器

- **默认生命周期**: 600 秒（10 分钟）
- **刷新间隔**: 60 秒前刷新（建议）
- **Refresh Request**: 更新 LIFETIME 属性

```c
// 伪代码：TURN 刷新
void turn_refresh_timer()
{
    // 每 540 秒（9 分钟）刷新一次
    turn_agent_refresh(stun, protocol, local, turn, 600, &credential, &id);
}
```

#### 5.5.3 STUN Keep-alive

- **间隔**: 15-30 秒
- **方法**: STUN Binding Indication（无响应）
- **目的**: 保持 NAT 绑定活跃

```c
// 伪代码：STUN Keep-alive
void stun_keepalive_timer()
{
    // 每 15 秒发送一次
    stun_agent_indication(stun, protocol, local, remote);
}
```

## 6. 使用示例

### 6.1 基本 ICE 建立连接示例

```c
#include <ice-agent.h>

// 事件回调
int on_send(void* param, int protocol,
            const struct sockaddr* local, const struct sockaddr* remote,
            const void* data, int bytes)
{
    // 实现底层 UDP/TCP 发送
    return sendto(socket_fd, data, bytes, 0, remote, sizeof(*remote));
}

void on_data(void* param, uint8_t stream, uint16_t component,
             const void* data, int bytes)
{
    // 接收到应用数据（RTP/RTCP）
    printf("Received %d bytes on stream %d component %d\n", bytes, stream, component);
}

void on_gather(void* param, int code)
{
    if (code == 0) {
        printf("Candidate gathering completed\n");
        // 获取本地候选地址列表并通过 SIP/SDP 发送给对端
    }
}

void on_connected(void* param, uint64_t flags, uint64_t mask)
{
    printf("ICE connected! flags=0x%llx mask=0x%llx\n", flags, mask);
    // 可以开始发送媒体数据
}

int main()
{
    // 1. 创建 ICE Agent（Controlling 角色）
    struct ice_agent_handler_t handler = {
        .send = on_send,
        .ondata = on_data,
        .ongather = on_gather,
        .onconnected = on_connected,
    };

    struct ice_agent_t* ice = ice_agent_create(1, &handler, NULL);

    // 2. 添加本地候选地址
    struct sockaddr_in host_addr;
    host_addr.sin_family = AF_INET;
    host_addr.sin_port = htons(5000);
    inet_pton(AF_INET, "192.168.1.100", &host_addr.sin_addr);

    ice_agent_add_local_candidate(ice, 1, 1, IPPROTO_UDP,
                                  (struct sockaddr_storage*)&host_addr);

    // 3. 通过 STUN 收集 Server Reflexive 候选地址
    struct sockaddr_in stun_server;
    stun_server.sin_family = AF_INET;
    stun_server.sin_port = htons(3478);
    inet_pton(AF_INET, "stun.example.com", &stun_server.sin_addr);

    ice_agent_gather(ice, 1, 1, IPPROTO_UDP,
                     (struct sockaddr_storage*)&host_addr,
                     (struct sockaddr_storage*)&stun_server,
                     NULL, NULL, NULL);

    // 4. 等待 ongather() 回调后，交换候选地址（通过 SIP）

    // 5. 添加远端候选地址（从 SIP SDP 接收）
    struct sockaddr_in remote_addr;
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(6000);
    inet_pton(AF_INET, "192.168.1.200", &remote_addr.sin_addr);

    ice_agent_add_remote_candidate(ice, 1, 1, IPPROTO_UDP,
                                   "foundation1", 2130706431,
                                   (struct sockaddr_storage*)&remote_addr);

    // 6. 启动连接性检查
    ice_agent_start(ice, 1, "localuser", "localpwd", "remoteuser", "remotepwd");

    // 7. 主循环：接收网络数据
    while (running) {
        uint8_t buffer[2048];
        struct sockaddr_storage remote;
        socklen_t addrlen = sizeof(remote);

        int n = recvfrom(socket_fd, buffer, sizeof(buffer), 0,
                        (struct sockaddr*)&remote, &addrlen);
        if (n > 0) {
            // 输入到 ICE Agent 处理
            ice_agent_input(ice, IPPROTO_UDP,
                          (struct sockaddr*)&host_addr,
                          (struct sockaddr*)&remote,
                          buffer, n);
        }
    }

    // 8. 发送 RTP 数据
    uint8_t rtp_packet[200];
    // ... 填充 RTP 包 ...
    ice_agent_send(ice, 1, 1, rtp_packet, sizeof(rtp_packet));

    // 9. 清理
    ice_agent_destroy(&ice);

    return 0;
}
```

### 6.2 TURN 中继示例

```c
#include <stun-agent.h>

void on_allocate_response(void* param, const struct stun_message_t* resp)
{
    // 解析 XOR-RELAYED-ADDRESS
    const struct stun_attr_t* attr = stun_message_attr_find(resp, STUN_ATTR_XOR_RELAYED_ADDRESS);
    if (attr) {
        struct sockaddr_storage relayed_addr;
        stun_attr_address(attr, &relayed_addr);

        // 获得 TURN 分配的中继地址
        printf("TURN relayed address allocated\n");

        // 将此地址作为 Relayed 候选地址添加到 ICE
    }
}

int main()
{
    struct stun_agent_handler_t handler = {
        .send = on_send,
        .ondata = on_data,
        // ... 其他回调 ...
    };

    struct stun_agent_t* stun = stun_agent_create(STUN_RFC_5389, &handler, NULL);

    // 准备 TURN 凭证
    struct stun_credential_t credential;
    credential.credential = STUN_CREDENTIAL_LONG_TERM;
    strcpy(credential.usr, "turnuser");
    strcpy(credential.pwd, "turnpass");

    // 发送 TURN Allocate 请求
    struct sockaddr_in turn_server;
    turn_server.sin_family = AF_INET;
    turn_server.sin_port = htons(3478);
    inet_pton(AF_INET, "turn.example.com", &turn_server.sin_addr);

    void* txn_id;
    turn_agent_allocate(stun, IPPROTO_UDP,
                       (struct sockaddr*)&local_addr,
                       (struct sockaddr*)&turn_server,
                       600,  // 10 分钟生命周期
                       &credential, &txn_id);

    // 接收网络数据并处理响应...

    // 创建权限（允许与 Peer 通信）
    struct sockaddr_in peer_addr;
    // ... 初始化 peer_addr ...
    turn_agent_create_permission(stun, IPPROTO_UDP,
                                (struct sockaddr*)&local_addr,
                                (struct sockaddr*)&turn_server,
                                (struct sockaddr*)&peer_addr,
                                &credential, &txn_id);

    // 绑定通道（优化）
    turn_agent_channel_bind(stun, IPPROTO_UDP,
                          (struct sockaddr*)&local_addr,
                          (struct sockaddr*)&turn_server,
                          (struct sockaddr*)&peer_addr,
                          0x4000,  // 通道号
                          &credential, &txn_id);

    // 清理
    stun_agent_destroy(&stun);

    return 0;
}
```

### 6.3 STUN Binding 请求示例

```c
#include <stun-agent.h>
#include <stun-message.h>

void on_bind_response(void* param, const struct stun_message_t* resp)
{
    // 解析 XOR-MAPPED-ADDRESS
    const struct stun_attr_t* attr = stun_message_attr_find(resp,
                                                             STUN_ATTR_XOR_MAPPED_ADDRESS);
    if (attr) {
        struct sockaddr_storage mapped_addr;
        stun_attr_address(attr, &mapped_addr);

        char ip[64];
        uint16_t port;

        if (mapped_addr.ss_family == AF_INET) {
            struct sockaddr_in* addr4 = (struct sockaddr_in*)&mapped_addr;
            inet_ntop(AF_INET, &addr4->sin_addr, ip, sizeof(ip));
            port = ntohs(addr4->sin_port);
        }

        printf("Mapped address: %s:%d\n", ip, port);
        // 这就是你的公网 IP 和端口（Server Reflexive 候选地址）
    }
}

int main()
{
    struct stun_agent_handler_t handler = {
        .send = on_send,
        // ... 其他回调 ...
    };

    struct stun_agent_t* stun = stun_agent_create(STUN_RFC_5389, &handler, NULL);

    // 发送 STUN Binding 请求
    struct sockaddr_in stun_server;
    stun_server.sin_family = AF_INET;
    stun_server.sin_port = htons(3478);
    inet_pton(AF_INET, "stun.l.google.com", &stun_server.sin_addr);

    void* txn_id;
    stun_agent_bind(stun, IPPROTO_UDP,
                   (struct sockaddr*)&local_addr,
                   (struct sockaddr*)&stun_server,
                   0, 0, 0, 0,  // 非 ICE 用途，这些参数为 0
                   NULL, &txn_id);

    // 接收网络数据并处理响应...
    // 在响应中调用 on_bind_response()

    stun_agent_destroy(&stun);
    return 0;
}
```

## 7. 性能和优化

### 7.1 连接建立时间优化

- **Aggressive Nomination**：Controlling 在第一次成功检查时立即提名，减少建立时间
- **并行检查**：同时对多个候选地址对进行检查
- **优先级排序**：优先检查高优先级候选地址对（Host > Peer Reflexive > Server Reflexive > Relayed）

### 7.2 带宽优化

- **TURN 通道绑定**：使用 ChannelBind 减少头部开销（36 字节 → 4 字节）
- **候选地址裁剪**：限制候选地址数量，避免过多检查
- **ICE-Lite**：受限环境下使用简化的 ICE 实现

### 7.3 内存优化

- **候选地址池管理**：使用内存池减少频繁分配
- **事务管理**：及时清理完成的 STUN 事务
- **检查列表剪枝**：移除失败的候选地址对

## 8. 常见问题

### 8.1 ICE 建立失败

**原因**：
- 所有候选地址对检查都失败
- 防火墙阻止 UDP 通信
- 对称 NAT 且无 TURN 服务器

**解决方案**：
- 确保至少有 TURN 服务器作为后备
- 检查网络连接和防火墙规则
- 增加连接建立超时时间

### 8.2 STUN 请求超时

**原因**：
- STUN 服务器不可达
- 网络延迟过高
- UDP 包被防火墙丢弃

**解决方案**：
- 使用可靠的 STUN 服务器（如 Google STUN）
- 增加重传次数
- 尝试 TCP 传输

### 8.3 TURN 分配失败

**原因**：
- 认证失败（用户名/密码错误）
- TURN 服务器资源耗尽
- 配额限制

**解决方案**：
- 验证 TURN 凭证
- 检查 TURN 服务器日志
- 配置更多 TURN 服务器

### 8.4 媒体单向

**原因**：
- 只有一个方向的候选地址对成功
- 防火墙只允许单向流量
- NAT 绑定超时

**解决方案**：
- 发送 STUN Keep-alive 保持绑定
- 检查双向候选地址对状态
- 使用对称的 RTP/RTCP 端口对

## 9. 调试技巧

### 9.1 启用详细日志

```c
// 记录所有 STUN 消息
void log_stun_message(const struct stun_message_t* msg)
{
    printf("STUN Message: type=0x%04x length=%d\n",
           msg->header.msgtype, msg->header.length);

    for (int i = 0; i < msg->nattrs; i++) {
        printf("  Attr[%d]: type=0x%04x length=%d\n",
               i, msg->attrs[i].type, msg->attrs[i].length);
    }
}
```

### 9.2 监控候选地址对状态

```c
// 遍历所有候选地址对并打印状态
void dump_candidate_pairs(struct ice_agent_t* ice)
{
    // 遍历所有流
    struct list_head* pos;
    list_for_each(pos, &ice->streams) {
        struct ice_stream_t* stream = list_entry(pos, struct ice_stream_t, link);

        printf("Stream %d:\n", stream->stream);

        // 遍历检查列表
        for (int i = 0; i < stream->checklist->count; i++) {
            struct ice_candidate_pair_t* pair = &stream->checklist->pairs[i];

            printf("  Pair[%d]: state=%d nominated=%d priority=%llu\n",
                   i, pair->state, pair->nominated, pair->priority);
        }
    }
}
```

### 9.3 网络包抓取

使用 Wireshark 抓取 STUN/TURN 流量：
```
# 过滤 STUN/TURN 包
stun || turn

# 检查 STUN 属性
stun.type == 0x0001  # Binding Request
stun.type == 0x0101  # Binding Response
```

## 10. 与其他库的集成

### 10.1 与 libsip 集成

```c
// 在 SIP SDP 中交换 ICE 候选地址
void on_invite(const char* sdp)
{
    // 解析 SDP 中的 a=candidate 行
    // 格式：a=candidate:foundation component protocol priority ip port typ type
    // 示例：a=candidate:1 1 UDP 2130706431 192.168.1.100 5000 typ host

    char foundation[33];
    int component, protocol;
    uint32_t priority;
    char ip[64];
    uint16_t port;
    char type[32];

    sscanf(candidate_line, "a=candidate:%s %d %d %u %s %hu typ %s",
           foundation, &component, &protocol, &priority, ip, &port, type);

    struct sockaddr_in remote_addr;
    remote_addr.sin_family = AF_INET;
    remote_addr.sin_port = htons(port);
    inet_pton(AF_INET, ip, &remote_addr.sin_addr);

    ice_agent_add_remote_candidate(ice, 1, component, protocol,
                                   foundation, priority,
                                   (struct sockaddr_storage*)&remote_addr);
}
```

### 10.2 与 librtp 集成

```c
// 通过 ICE 发送 RTP 包
void send_rtp_packet(uint8_t* rtp_packet, int length)
{
    ice_agent_send(ice, 1, 1, rtp_packet, length);
}

// ICE ondata 回调接收 RTP 包
void on_ice_data(void* param, uint8_t stream, uint16_t component,
                 const void* data, int bytes)
{
    if (component == 1) {
        // RTP 包
        rtp_packet_input((rtp_packet_t*)data, bytes);
    } else if (component == 2) {
        // RTCP 包
        rtcp_packet_input((rtcp_packet_t*)data, bytes);
    }
}
```

## 11. 总结

libice 提供了完整的 ICE/STUN/TURN 协议实现，包括：

- **ICE 核心功能**：候选地址收集、连接性检查、提名机制
- **STUN 协议**：Binding 请求/响应、Keep-alive、认证
- **TURN 协议**：中继分配、权限管理、通道绑定
- **灵活的 API**：支持多流、多组件、TCP/UDP 传输
- **事件驱动**：异步回调模型，易于集成

通过合理使用 libice，可以为实时通信应用提供可靠的 NAT 穿透能力，确保在各种网络环境下建立稳定的对等连接。

## 参考资料

- RFC 5245 - Interactive Connectivity Establishment (ICE)
- RFC 8445 - ICE: A Protocol for NAT Traversal (Updated)
- RFC 5389 - Session Traversal Utilities for NAT (STUN)
- RFC 5766 - Traversal Using Relays around NAT (TURN)
- RFC 5245 Section 4.1.2 - Computing Priorities
- RFC 5389 Section 15 - STUN Attributes
- RFC 5766 Section 6 - TURN Commands
