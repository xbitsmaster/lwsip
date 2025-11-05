# Transport Layer Abstraction Design

## 概述

v2.0引入了传输层抽象，使lwsip可以在各种通信场景下使用，而不仅限于标准的TCP/UDP socket。

## 设计动机

### 嵌入式系统的多样性

在实际的嵌入式应用中，设备可能处于各种网络环境：

1. **标准网络环境**
   - 以太网、WiFi
   - 可以直接使用TCP/UDP socket
   - 示例：工业网关、智能家居主机

2. **IoT/物联网环境**
   - 使用MQTT协议通信
   - 设备通过MQTT broker中转消息
   - 示例：传感器节点、远程监控设备

3. **工业现场环境**
   - RS232/RS485串口通信
   - 没有网络连接
   - 示例：PLC、工业控制器

4. **自定义协议环境**
   - 专有加密协议
   - 特殊的数据封装
   - 示例：军工、航空航天

## 架构设计

### 抽象层次

```
┌──────────────────────────────────────┐
│      lws_client (SIP层)              │
│                                      │
│  lws_client_create()                │
│  lws_client_loop()                  │
└──────────────────────────────────────┘
               ↓ (使用)
┌──────────────────────────────────────┐
│   lws_transport (传输抽象层)          │
│                                      │
│  lws_transport_connect()            │
│  lws_transport_send()               │
│  lws_transport_poll()               │
└──────────────────────────────────────┘
               ↓ (实现)
┌──────────────────────────────────────┐
│   具体传输实现                        │
│                                      │
│  • lws_transport_tcp.c  (TCP/UDP)   │
│  • lws_transport_mqtt.c (MQTT)      │
│  • lws_transport_serial.c (串口)    │
│  • lws_transport_xxx.c  (自定义)    │
└──────────────────────────────────────┘
```

### 核心接口

```c
// 传输层抽象接口
struct lws_transport {
    const lws_transport_ops_t* ops;  // 虚函数表
    lws_transport_config_t config;
    lws_transport_handler_t handler;
    lws_transport_state_t state;
};

// 操作函数表（类似C++虚函数表）
typedef struct {
    int (*connect)(lws_transport_t* transport);
    void (*disconnect)(lws_transport_t* transport);
    int (*send)(lws_transport_t* transport, const void* data, int len);
    lws_transport_state_t (*get_state)(lws_transport_t* transport);
    int (*get_local_addr)(lws_transport_t* transport, char* ip, uint16_t* port);
    int (*poll)(lws_transport_t* transport, int timeout_ms);
    void (*destroy)(lws_transport_t* transport);
} lws_transport_ops_t;
```

## 已实现的传输

### 1. TCP/UDP Socket传输 (`lws_transport_tcp.c`)

**用途**: 标准SIP over TCP/UDP

**特性**:
- ✅ 支持TCP和UDP
- ✅ 非阻塞I/O
- ✅ 自动重连（TCP）
- ✅ 支持IPv4（IPv6可扩展）

**使用示例**:
```c
lws_transport_config_t config = {
    .remote_host = "192.168.1.100",
    .remote_port = 5060,
    .local_port = 5060,
    .use_tcp = 0,  // 0=UDP, 1=TCP
};

lws_transport_handler_t handler = {
    .on_recv = on_recv_callback,
    .on_state = on_state_callback,
};

lws_transport_t* transport = lws_transport_tcp_create(&config, &handler);
lws_transport_connect(transport);
```

### 2. MQTT传输 (`lws_transport_mqtt.c`)

**用途**: 通过MQTT broker转发SIP消息

**应用场景**:
- 设备在NAT/防火墙后，无法接收入站连接
- 已有MQTT基础设施
- 低功耗IoT设备

**消息流程**:
```
设备A                 MQTT Broker             设备B
  |                       |                     |
  |-- PUBLISH(topic_B) -->|                     |
  |   (SIP INVITE)        |--- DELIVER -------> |
  |                       |                     |
  |                       |<-- PUBLISH(topic_A)-|
  |<-- DELIVER -----------|   (SIP 200 OK)      |
```

**配置示例**:
```c
lws_transport_config_t config = {
    .remote_host = "mqtt.example.com",
    .remote_port = 1883,
    .mqtt_client_id = "device001",
    .mqtt_pub_topic = "sip/device002/inbox",  // 发送到对方的topic
    .mqtt_sub_topic = "sip/device001/inbox",  // 自己订阅的topic
};

lws_transport_t* transport = lws_transport_mqtt_create(&config, &handler);
```

**优点**:
- ✅ 穿越NAT/防火墙
- ✅ 低功耗（MQTT支持持久连接）
- ✅ QoS保证消息可靠性
- ✅ 支持TLS加密

**缺点**:
- ❌ 需要MQTT broker中间件
- ❌ 延迟较高（经过broker转发）
- ❌ 不适合实时性要求高的场景

## 如何实现自定义传输

### 步骤1: 定义传输结构

```c
typedef struct lws_transport_xxx {
    lws_transport_t base;  // 必须作为第一个成员！

    // 自定义字段
    int your_handle;
    char your_buffer[1024];
} lws_transport_xxx_t;
```

### 步骤2: 实现操作函数

```c
static int xxx_connect(lws_transport_t* transport) {
    lws_transport_xxx_t* xxx = (lws_transport_xxx_t*)transport;
    // 实现连接逻辑
    return LWS_OK;
}

static int xxx_send(lws_transport_t* transport, const void* data, int len) {
    lws_transport_xxx_t* xxx = (lws_transport_xxx_t*)transport;
    // 实现发送逻辑
    return len;
}

// ... 实现其他函数
```

### 步骤3: 定义操作表

```c
static const lws_transport_ops_t xxx_ops = {
    .connect = xxx_connect,
    .disconnect = xxx_disconnect,
    .send = xxx_send,
    .get_state = xxx_get_state,
    .get_local_addr = xxx_get_local_addr,
    .poll = xxx_poll,
    .destroy = xxx_destroy,
};
```

### 步骤4: 实现工厂函数

```c
lws_transport_t* lws_transport_xxx_create(
    const lws_transport_config_t* config,
    const lws_transport_handler_t* handler)
{
    lws_transport_xxx_t* xxx = lws_malloc(sizeof(lws_transport_xxx_t));

    memset(xxx, 0, sizeof(lws_transport_xxx_t));
    xxx->base.ops = &xxx_ops;  // 设置虚函数表
    // ... 初始化

    return &xxx->base;
}
```

### 步骤5: 在lws_client.c中使用

```c
// 在lws_client_create()中选择传输
#ifdef USE_MQTT_TRANSPORT
    client->transport = lws_transport_mqtt_create(&transport_config, &transport_handler);
#else
    client->transport = lws_transport_tcp_create(&transport_config, &transport_handler);
#endif
```

## 设计模式说明

### 虚函数表模式（C语言实现OOP）

```c
// 基类（抽象类）
struct lws_transport {
    const lws_transport_ops_t* ops;  // "虚函数表"
    // ... 其他成员
};

// 子类必须将基类作为第一个成员
struct lws_transport_tcp {
    lws_transport_t base;  // 继承
    // ... TCP特有成员
};

// 多态调用
lws_transport_send(transport, data, len);
// 等价于：transport->ops->send(transport, data, len);
```

这种设计：
- ✅ 实现了多态性
- ✅ 类型安全
- ✅ 零运行时开销（函数指针直接调用）

### 依赖倒置原则（DIP）

```
高层模块 (lws_client)
    ↓ 依赖抽象
抽象层 (lws_transport)
    ↑ 实现抽象
底层模块 (lws_transport_tcp, lws_transport_mqtt)
```

- 高层模块（lws_client）不依赖底层实现
- 都依赖抽象接口（lws_transport.h）
- 可以随时替换底层实现，不影响上层

## 传输选择指南

| 场景 | 推荐传输 | 原因 |
|-----|---------|------|
| **标准SIP应用** | TCP/UDP | 兼容性好、性能高 |
| **设备在NAT后** | MQTT | 穿越NAT |
| **低功耗IoT** | MQTT | 支持休眠、QoS |
| **工业现场** | 串口 | 无网络环境 |
| **安全敏感** | 自定义+加密 | 满足特殊安全要求 |

## 性能对比

| 传输 | 延迟 | 吞吐量 | CPU占用 | 内存占用 |
|-----|------|--------|---------|----------|
| **TCP** | ~10ms | 高 | 低 | 低 |
| **UDP** | ~5ms | 高 | 极低 | 极低 |
| **MQTT** | ~100ms | 中 | 中 | 中 |
| **串口** | ~50ms | 低 | 低 | 极低 |

## 未来扩展

### 可能的传输实现

1. **lws_transport_serial.c** - 串口传输
   - RS232/RS485/RS422
   - 适用于工业控制

2. **lws_transport_websocket.c** - WebSocket传输
   - 适用于浏览器集成
   - 支持SIP over WebSocket (RFC 7118)

3. **lws_transport_ble.c** - 蓝牙BLE传输
   - 低功耗
   - 短距离通信

4. **lws_transport_lora.c** - LoRa传输
   - 远距离、低功耗
   - 适用于农业、环境监测

## 参考文献

- RFC 3261: SIP (Session Initiation Protocol)
- RFC 7118: SIP over WebSocket
- MQTT v3.1.1 / v5.0 Specification
- Design Patterns: Elements of Reusable Object-Oriented Software

---

**设计者**: lwsip v2.0 team
**版本**: 2025-01
