# LWSIP OSAL (OS Abstraction Layer)

用于 lwsip 项目的操作系统抽象层，提供跨平台的统一接口。

## 支持的平台

- ✅ Linux (使用 pthread)
- ✅ macOS (使用 pthread 和 os_unfair_lock)
- 🔄 FreeRTOS (计划中)
- 🔄 Zephyr (计划中)
- 🔄 RT-Thread (计划中)

## 功能模块

### 1. 线程管理 (lws_thread.h)
- 创建和销毁线程
- 线程加入和分离
- 线程睡眠
- 获取线程ID

### 2. 互斥锁 (lws_mutex.h)
- 创建和销毁互斥锁
- 初始化和清理互斥锁
- 支持栈分配（零 malloc 开销）
- 加锁和解锁
- 尝试加锁（非阻塞）

### 3. 自旋锁 (lws_spinlock.h)
- 初始化和销毁自旋锁
- 加锁和解锁（忙等待）
- 尝试加锁（非阻塞）

### 4. 内存管理 (lws_mem.h)
- 内存分配和释放
- 字符串复制

### 5. 日志系统 (lws_log.h)
- 多级日志输出
- ERROR/WARN/FATAL 带错误码
- DEBUG/INFO/TRACE 不带错误码

## 构建

### 使用 CMake（推荐）

CMake 构建系统支持通过 `-DTHREAD` 参数选择平台：

```bash
# 默认构建（自动检测 pthread 平台：Linux 或 macOS）
mkdir build
cd build
cmake ../osal
make

# 显式指定 pthread 平台
cmake -DTHREAD=pthread ../osal
make

# 指定 FreeRTOS 平台（待支持）
cmake -DTHREAD=freertos ../osal
make

# 指定 Zephyr 平台（待支持）
cmake -DTHREAD=zephyr ../osal
make

# 指定 RT-Thread 平台（待支持）
cmake -DTHREAD=rtthread ../osal
make
```

构建产物：
- 静态库：`build/lib/liblwsosal.a`
- 头文件：`osal/include/*.h`

平台宏定义：
- pthread: `__LWS_PTHREAD__`（默认）
- FreeRTOS: `__LWS_FREERTOS__`
- Zephyr: `__LWS_ZEPHYR__`
- RT-Thread: `__LWS_RTTHREAD__`

### 使用 Makefile

```bash
# 构建 OSAL 库
cd osal
make

# 清理构建产物
make clean

# 安装到系统目录
make install

# 卸载
make uninstall
```

构建产物：
- 静态库：`osal/lib/liblwsosal.a`
- 头文件：`osal/include/*.h`

### 集成到主项目

在主项目的 CMakeLists.txt 或 Makefile 中：

```cmake
# CMakeLists.txt
include_directories(${CMAKE_SOURCE_DIR}/osal/include)
link_directories(${CMAKE_SOURCE_DIR}/osal/lib)
target_compile_definitions(your_target PRIVATE __LWS_PTHREAD__)
target_link_libraries(your_target lwsosal pthread)
```

或

```makefile
# Makefile
CFLAGS += -I./osal/include -D__LWS_PTHREAD__
LDFLAGS += -L./osal/lib -llwsosal -lpthread
```

**重要**：编译使用 OSAL 的代码时，必须定义平台宏（如 `-D__LWS_PTHREAD__`），以便头文件正确选择平台相关的类型定义。

编译示例：

```bash
# 使用 gcc 编译
gcc -D__LWS_PTHREAD__ -I./osal/include your_app.c -L./osal/lib -llwsosal -lpthread -o your_app

# 使用 CMake 构建的库
gcc -D__LWS_PTHREAD__ -I../osal/include your_app.c -L./build/lib -llwsosal -lpthread -o your_app
```

## 使用示例

### 线程

```c
#include "lws_osal.h"

int my_thread_func(void* arg) {
    LWS_LOG_INFO("", "Thread started\n");
    lws_thread_sleep(1000);  // Sleep 1 second
    return 0;
}

int main() {
    lws_thread_t* thread = lws_thread_create(my_thread_func, NULL);
    int retval;
    lws_thread_join(thread, &retval);
    lws_thread_destroy(thread);
    return 0;
}
```

### 互斥锁

#### 方式1: 栈分配（零 malloc，推荐）

```c
#include "lws_osal.h"

/* 栈上分配 - 零 malloc 开销 */
lws_mutex_t mutex;
lws_mutex_init(&mutex);

lws_mutex_lock(&mutex);
// Critical section
lws_mutex_unlock(&mutex);

lws_mutex_cleanup(&mutex);  /* 清理，不释放内存 */
```

#### 方式2: 堆分配（传统方式）

```c
#include "lws_osal.h"

/* 堆分配 - 1次 malloc */
lws_mutex_t* mutex = lws_mutex_create();

lws_mutex_lock(mutex);
// Critical section
lws_mutex_unlock(mutex);

lws_mutex_destroy(mutex);  /* 清理并释放内存 */
```

### 自旋锁

```c
#include "lws_osal.h"

lws_spinlock_t lock;
lws_spinlock_init(&lock);

lws_spinlock_lock(&lock);
// Very short critical section
lws_spinlock_unlock(&lock);

lws_spinlock_destroy(&lock);
```

### 日志

```c
#include "lws_osal.h"

// 带错误码的日志
LWS_LOG_ERROR(-1, "Failed to open file: %s\n", filename);
LWS_LOG_WARN(0, "Connection timeout\n");

// 不带错误码的日志
LWS_LOG_INFO("", "Server started on port %d\n", port);
LWS_LOG_DEBUG("", "Processing request\n");
```

## 平台差异

### Linux vs macOS

| 功能 | Linux | macOS |
|------|-------|-------|
| 线程 | pthread | pthread |
| 互斥锁 | pthread_mutex | pthread_mutex |
| 自旋锁 | pthread_spinlock | os_unfair_lock |
| 内存 | stdlib | stdlib |

### 关键差异说明

1. **自旋锁**：
   - Linux: 使用 `pthread_spinlock_t`
   - macOS: 使用 `os_unfair_lock`（OSSpinLock 已废弃）

2. **字符串复制**：
   - Linux: 使用 `strdup/strndup`
   - macOS: 手动实现（某些版本可能没有 strndup）

## 目录结构

```
osal/
├── include/              # 头文件
│   ├── lws_thread.h
│   ├── lws_mutex.h
│   ├── lws_spinlock.h
│   ├── lws_mem.h
│   ├── lws_log.h
│   └── lws_osal.h       # 主头文件
├── src/
│   ├── linux/           # Linux 实现
│   │   ├── lws_thread.c
│   │   ├── lws_mutex.c
│   │   ├── lws_spinlock.c
│   │   ├── lws_mem.c
│   │   ├── lws_log.c
│   │   └── lws_osal.c
│   └── macos/           # macOS 实现
│       ├── lws_thread.c
│       ├── lws_mutex.c
│       ├── lws_spinlock.c
│       ├── lws_mem.c
│       ├── lws_log.c
│       └── lws_osal.c
├── Makefile
└── README.md
```

## API 约定

1. **命名规范**：所有 API 使用 `lws_` 前缀
2. **返回值**：成功返回 0，失败返回 -1（除非另有说明）
3. **空指针检查**：所有 API 都进行空指针检查
4. **线程安全**：互斥锁和自旋锁是线程安全的
5. **资源管理**：调用者负责释放创建的资源

## 移植到新平台

要移植到新平台（如 FreeRTOS），需要：

1. 创建新目录：`osal/src/freertos/`
2. 实现所有模块：
   - `lws_thread.c`
   - `lws_mutex.c`
   - `lws_spinlock.c`
   - `lws_mem.c`
   - `lws_log.c`
   - `lws_osal.c`
3. 更新 Makefile 添加平台检测
4. 测试所有 API

## 测试

待补充单元测试。

## 许可证

与主项目保持一致。
