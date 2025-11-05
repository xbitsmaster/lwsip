# Lwsip 编码规范

## 结构体定义规范

### 规则

所有结构体必须遵循以下定义方式：

```c
/* 1. 先定义结构体 */
struct my_structure {
    int field1;
    char field2[64];
    void* field3;
};

/* 2. 然后定义类型别名 */
typedef struct my_structure my_structure_t;
```

### ❌ 错误示例

```c
/* 不要使用以下方式 */

/* 方式1: 错误 - 直接在typedef中定义 */
typedef struct my_structure_t {
    int field1;
} my_structure_t;

/* 方式2: 错误 - 匿名结构体 */
typedef struct {
    int field1;
} my_structure_t;

/* 方式3: 错误 - 仅前向声明typedef */
typedef struct my_structure_t my_structure_t;
struct my_structure_t {
    int field1;
};
```

### ✅ 正确示例

```c
/* 方式: 先定义结构体，再typedef */
struct my_structure {
    int field1;
    char field2[64];
};
typedef struct my_structure my_structure_t;
```

## 代码示例

### 公共头文件 (lws_types.h)

```c
/* 前向声明 */
struct lws_client;
typedef struct lws_client lws_client_t;

struct lws_config;
typedef struct lws_config lws_config_t;

/* 完整定义 */
struct lws_config {
    char server_host[128];
    uint16_t server_port;
    char username[64];
    char password[64];
};

struct lws_callbacks {
    void (*on_event)(void* userdata);
    void* userdata;
};
typedef struct lws_callbacks lws_callbacks_t;
```

### 实现文件 (lws_client.c)

```c
/* 完整定义（在.c文件中隐藏实现细节） */
struct lws_client {
    lws_config_t config;
    lws_callbacks_t callbacks;
    int state;
    void* private_data;
};

/* 使用 sizeof 时使用结构体名 */
lws_client_t* lws_client_create(void)
{
    lws_client_t* client = calloc(1, sizeof(struct lws_client));
    return client;
}
```

## 命名约定

### 结构体命名

- 结构体名称使用小写字母和下划线：`struct lws_client`
- 类型别名添加 `_t` 后缀：`lws_client_t`
- 使用有意义的名称，体现结构体的用途

### 示例

```c
/* 传输层上下文 */
struct sip_transport_ctx {
    int socket;
    void* handler;
};
typedef struct sip_transport_ctx sip_transport_ctx_t;

/* 媒体流 */
struct media_stream {
    int stream_index;
    void* rtp;
    void* encoder;
};
typedef struct media_stream media_stream_t;

/* 会话对象 */
struct lws_session {
    char peer_uri[256];
    int call_state;
};
typedef struct lws_session lws_session_t;
```

## 前向声明

当需要在头文件中使用指针类型，但不需要完整定义时，使用前向声明：

```c
/* 头文件 */
struct lws_client;
typedef struct lws_client lws_client_t;

/* 函数声明中可以使用指针 */
lws_client_t* lws_client_create(void);
void lws_client_destroy(lws_client_t* client);
```

## 代码格式化

### 使用 clang-format

项目使用 clang-format 进行代码格式化。配置文件 `.clang-format`:

```yaml
BasedOnStyle: LLVM
IndentWidth: 4
TabWidth: 4
UseTab: Never
ColumnLimit: 100
BreakBeforeBraces: Linux
AllowShortFunctionsOnASingleLine: Empty
IndentCaseLabels: false
```

### 格式化命令

```bash
# 格式化单个文件
clang-format -i src/lws_client.c

# 格式化所有源文件
find src include -name "*.c" -o -name "*.h" | xargs clang-format -i
```

## 注释规范

### 文件头注释

```c
/**
 * @file lws_client.c
 * @brief SIP客户端核心实现
 * @author Your Name
 * @date 2025-01-15
 */
```

### 函数注释

```c
/**
 * 创建SIP客户端实例
 *
 * @param config 配置参数
 * @param callbacks 事件回调
 * @return 客户端实例，失败返回NULL
 */
lws_client_t* lws_client_create(const lws_config_t* config,
                                       const lws_callbacks_t* callbacks);
```

### 结构体字段注释

```c
struct lws_config {
    char server_host[128];      /* SIP服务器地址 */
    uint16_t server_port;       /* SIP服务器端口 */
    int register_expires;       /* 注册过期时间（秒） */
};
```

## 错误处理

### 检查指针参数

```c
lws_client_t* lws_client_create(const lws_config_t* config)
{
    if (!config)
        return NULL;

    lws_client_t* client = calloc(1, sizeof(struct lws_client));
    if (!client)
        return NULL;

    /* ... */
    return client;
}
```

### 资源清理

```c
void lws_client_destroy(lws_client_t* client)
{
    if (!client)
        return;

    /* 清理资源 */
    if (client->transport)
        sip_transport_destroy(client->transport);

    if (client->sip_agent)
        sip_agent_destroy(client->sip_agent);

    free(client);
}
```

## 线程安全

当结构体需要跨线程访问时，添加互斥锁：

```c
struct lws_client {
    pthread_mutex_t mutex;
    /* ... 其他字段 */
};

/* 初始化 */
pthread_mutex_init(&client->mutex, NULL);

/* 使用 */
pthread_mutex_lock(&client->mutex);
/* 访问共享数据 */
pthread_mutex_unlock(&client->mutex);

/* 清理 */
pthread_mutex_destroy(&client->mutex);
```

## 总结

- ✅ 使用 `struct name { }; typedef struct name name_t;` 格式
- ✅ 结构体名使用小写下划线，类型别名添加 `_t` 后缀
- ✅ 使用前向声明隐藏实现细节
- ✅ 在 `.c` 文件中定义完整结构体
- ✅ 使用 `sizeof(struct name)` 而不是 `sizeof(name_t)`
- ✅ 添加适当的注释
- ✅ 检查参数和返回值
- ✅ 正确管理资源生命周期
