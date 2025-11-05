# LWSIP OSAL (OS Abstraction Layer)

lwsip项目的操作系统抽象层，提供跨平台的统一接口。

## 支持的平台

- ✅ **Linux** (使用 pthread)
- ✅ **macOS** (使用 pthread 和 os_unfair_lock)
- 🔄 **FreeRTOS** (计划中)
- 🔄 **Zephyr** (计划中)
- 🔄 **RT-Thread** (计划中)

## 功能模块

### 1. 线程管理 (lws_thread.h)

- 创建和销毁线程
- 线程加入和分离
- 线程睡眠
- 获取线程ID

### 2. 互斥锁 (lws_mutex.h)

- 创建和销毁互斥锁
- 初始化和清理互斥锁
- 支持栈分配（零malloc开销）
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
- ERROR/WARN/FATAL带错误码
- DEBUG/INFO/TRACE不带错误码

## 构建

### 使用CMake（推荐）

OSAL作为lwsip项目的子模块，通过主项目的CMake构建：

```bash
# 在lwsip根目录
./build.sh

# 或手动构建
mkdir -p build && cd build
cmake ..
make

# 构建产物
# - 静态库: build/lib/liblwsosal.a
```

### 平台选择

CMake支持通过 `-DTHREAD` 参数选择目标平台：

```bash
# 默认构建（自动检测pthread平台：Linux或macOS）
cmake ..

# 显式指定pthread平台
cmake -DTHREAD=pthread ..

# FreeRTOS平台（待支持）
cmake -DTHREAD=freertos ..

# Zephyr平台（待支持）
cmake -DTHREAD=zephyr ..

# RT-Thread平台（待支持）
cmake -DTHREAD=rtthread ..
```

### 平台宏定义

编译时会自动定义以下宏：

- pthread: `__LWS_PTHREAD__`（默认）
- FreeRTOS: `__LWS_FREERTOS__`
- Zephyr: `__LWS_ZEPHYR__`
- RT-Thread: `__LWS_RTTHREAD__`

### 独立构建（用于测试）

如果需要单独编译OSAL：

```bash
cd osal
mkdir build && cd build
cmake ..
make

# 构建产物
# - 静态库: build/lib/liblwsosal.a
# - 示例程序: build/examples/*
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

#### 方式1: 栈分配（零malloc，推荐）

```c
#include "lws_osal.h"

/* 栈上分配 - 零malloc开销 */
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

/* 堆分配 - 1次malloc */
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

1. **自旋锁**:
   - Linux: 使用 `pthread_spinlock_t`
   - macOS: 使用 `os_unfair_lock` (OSSpinLock已废弃)

2. **字符串复制**:
   - Linux: 使用 `strdup/strndup`
   - macOS: 手动实现（某些版本可能没有strndup）

## 目录结构

```
osal/
├── include/              # 头文件
│   ├── lws_thread.h     # 线程接口
│   ├── lws_mutex.h      # 互斥锁接口
│   ├── lws_spinlock.h   # 自旋锁接口
│   ├── lws_mem.h        # 内存管理接口
│   ├── lws_log.h        # 日志接口
│   └── lws_osal.h       # 主头文件（包含所有）
│
├── src/
│   ├── linux/           # Linux实现
│   │   ├── lws_thread.c
│   │   ├── lws_mutex.c
│   │   ├── lws_spinlock.c
│   │   ├── lws_mem.c
│   │   ├── lws_log.c
│   │   └── lws_osal.c
│   │
│   └── macos/           # macOS实现
│       ├── lws_thread.c
│       ├── lws_mutex.c
│       ├── lws_spinlock.c
│       ├── lws_mem.c
│       ├── lws_log.c
│       └── lws_osal.c
│
├── examples/            # 使用示例
│   ├── thread_example.c
│   └── mutex_stack_example.c
│
├── CMakeLists.txt       # CMake配置
└── README.md            # 本文档
```

## 集成到其他项目

### CMake集成

```cmake
# 添加OSAL子目录
add_subdirectory(osal)

# 包含头文件
target_include_directories(your_target PRIVATE
    ${CMAKE_SOURCE_DIR}/osal/include
)

# 链接库
target_link_libraries(your_target PRIVATE
    lwsosal
    pthread  # Linux/macOS需要
)

# 定义平台宏（自动设置）
target_compile_definitions(your_target PRIVATE
    __LWS_PTHREAD__  # 或其他平台宏
)
```

### 手动集成

```bash
# 编译示例
gcc -D__LWS_PTHREAD__ \
    -I./osal/include \
    your_app.c \
    -L./build/lib \
    -llwsosal \
    -lpthread \
    -o your_app
```

**重要**: 编译使用OSAL的代码时，必须定义平台宏（如`-D__LWS_PTHREAD__`），以便头文件正确选择平台相关的类型定义。

## API约定

1. **命名规范**: 所有API使用`lws_`前缀
2. **返回值**: 成功返回0，失败返回-1（除非另有说明）
3. **空指针检查**: 所有API都进行空指针检查
4. **线程安全**: 互斥锁和自旋锁本身是线程安全的
5. **资源管理**: 调用者负责释放创建的资源

## 移植到新平台

要移植到新平台（如FreeRTOS），需要：

1. **创建新目录**: `osal/src/freertos/`

2. **实现所有模块**:
   - `lws_thread.c` - 线程管理
   - `lws_mutex.c` - 互斥锁
   - `lws_spinlock.c` - 自旋锁
   - `lws_mem.c` - 内存管理
   - `lws_log.c` - 日志系统
   - `lws_osal.c` - 初始化

3. **更新CMakeLists.txt**:
   ```cmake
   if(THREAD STREQUAL "freertos")
       set(PLATFORM_DIR freertos)
       set(PLATFORM_DEFINE __LWS_FREERTOS__)
       # 添加FreeRTOS特定的源文件和库
   endif()
   ```

4. **更新头文件**: 在`lws_thread.h`等头文件中添加平台特定的类型定义
   ```c
   #ifdef __LWS_FREERTOS__
       typedef TaskHandle_t lws_thread_native_t;
       // ...
   #endif
   ```

5. **测试**: 编写单元测试验证所有API

## 示例程序

OSAL提供了示例程序演示各模块的使用：

```bash
# 构建示例
cd osal/build
cmake ..
make

# 运行线程示例
./examples/thread_example

# 运行互斥锁示例
./examples/mutex_stack_example
```

## 性能考虑

1. **互斥锁 vs 自旋锁**:
   - 互斥锁: 适合长时间临界区，会让出CPU
   - 自旋锁: 适合极短临界区（几条指令），忙等待

2. **栈分配 vs 堆分配**:
   - 栈分配互斥锁: 零malloc开销，推荐用于性能关键路径
   - 堆分配互斥锁: 灵活但有malloc开销

3. **日志级别**:
   - 生产环境建议只启用ERROR/WARN
   - DEBUG/TRACE会影响性能

## 测试

TODO: 添加单元测试

## 许可证

与主项目lwsip保持一致。

---

**Version**: 1.0.0
**Last Updated**: 2025-11-05
