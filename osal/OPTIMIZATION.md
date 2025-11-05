# OSAL 优化记录

## 零成本抽象优化 (2025-01-04)

### 背景

OSAL 最初设计时使用了不透明指针（opaque pointer），所有结构体都隐藏在实现文件中：

```c
/* 之前：不透明指针 */
struct lws_mutex;  // 仅前向声明
typedef struct lws_mutex lws_mutex_t;

/* 实际定义在 .c 文件中 */
struct lws_mutex {
    pthread_mutex_t handle;
};
```

这种设计的问题：
1. **必须动态分配**：用户无法在栈上分配
2. **额外开销**：每次使用都需要 malloc/free
3. **性能损失**：增加内存分配和释放的开销

### 优化方案

由于 Linux 和 macOS 都使用 pthread，我们可以直接在头文件中暴露结构体定义：

```c
/* 现在：直接暴露结构体 */
#include <pthread.h>

struct lws_mutex {
    pthread_mutex_t handle;
};
typedef struct lws_mutex lws_mutex_t;
```

### 优势

#### 1. 支持栈分配

```c
/* 零 malloc 开销 */
lws_mutex_t mutex;  // 栈上分配
lws_mutex_init(&mutex);
// 使用
lws_mutex_cleanup(&mutex);
```

#### 2. 减少内存分配

| 场景 | 之前 | 现在 | 改进 |
|------|------|------|------|
| 单个 mutex | 1次 malloc | 0次 malloc | ✅ 100% |
| 10个 mutex | 10次 malloc | 0次 malloc | ✅ 100% |

#### 3. 更好的性能

- **减少内存碎片**：栈分配无碎片
- **更快的分配速度**：栈分配比堆分配快约 10-100 倍
- **更好的缓存局部性**：连续的栈空间

#### 4. 灵活性

仍然支持堆分配，用户可以根据需求选择：

```c
/* 栈分配 - 适合短生命周期 */
lws_mutex_t mutex;
lws_mutex_init(&mutex);
lws_mutex_cleanup(&mutex);

/* 堆分配 - 适合长生命周期或动态数量 */
lws_mutex_t* mutex = lws_mutex_create();
lws_mutex_destroy(mutex);
```

### API 设计

#### 新增函数

```c
/* 初始化（不分配内存） */
void lws_mutex_init(lws_mutex_t* mutex);

/* 清理（不释放内存） */
void lws_mutex_cleanup(lws_mutex_t* mutex);
```

#### 现有函数

```c
/* 创建（分配+初始化） */
lws_mutex_t* lws_mutex_create(void);

/* 销毁（清理+释放） */
void lws_mutex_destroy(lws_mutex_t* mutex);
```

### 实现细节

#### lws_mutex_init
```c
void lws_mutex_init(lws_mutex_t* mutex)
{
    if (mutex)
        pthread_mutex_init(&mutex->handle, NULL);
}
```

#### lws_mutex_cleanup
```c
void lws_mutex_cleanup(lws_mutex_t* mutex)
{
    if (mutex)
        pthread_mutex_destroy(&mutex->handle);
}
```

#### lws_mutex_create
```c
lws_mutex_t* lws_mutex_create(void)
{
    lws_mutex_t* mutex = lws_calloc(1, sizeof(lws_mutex_t));
    if (!mutex) return NULL;
    if (pthread_mutex_init(&mutex->handle, NULL) != 0) {
        lws_free(mutex);
        return NULL;
    }
    return mutex;
}
```

#### lws_mutex_destroy
```c
void lws_mutex_destroy(lws_mutex_t* mutex)
{
    if (mutex) {
        pthread_mutex_destroy(&mutex->handle);
        lws_free(mutex);
    }
}
```

### 性能测试结果

测试场景：3个线程同时访问共享计数器，每个线程递增1000次

```
=== Method 1: Stack Allocation (Zero malloc) ===
Shared counter: 3000 (expected: 3000) ✓

=== Method 2: Heap Allocation (Traditional) ===
Shared counter: 3000 (expected: 3000) ✓

Summary:
✓ Stack allocation: 0 malloc calls for mutex
✓ Heap allocation: 1 malloc call for mutex
✓ Both methods work correctly!
```

### 应用到其他模块

同样的优化也应用到了 `lws_thread_t`：

```c
/* 直接暴露结构体 */
struct lws_thread {
    pthread_t handle;
};
typedef struct lws_thread lws_thread_t;
```

虽然 thread 通常是堆分配的（因为需要返回句柄），但这个设计为未来的优化留下了空间。

### 移植性考虑

对于不使用 pthread 的平台（如 FreeRTOS），可以使用条件编译：

```c
#ifdef __PTHREAD__
struct lws_mutex {
    pthread_mutex_t handle;
};
#elif defined(__FREERTOS__)
struct lws_mutex {
    SemaphoreHandle_t handle;
};
#else
struct lws_mutex;  // 不透明指针
#endif
```

### 最佳实践

#### 短生命周期 - 使用栈分配

```c
void function(void)
{
    lws_mutex_t mutex;
    lws_mutex_init(&mutex);

    // 使用 mutex

    lws_mutex_cleanup(&mutex);
}
```

#### 长生命周期 - 使用堆分配

```c
struct my_object {
    lws_mutex_t* mutex;
};

void init_object(struct my_object* obj)
{
    obj->mutex = lws_mutex_create();
}

void cleanup_object(struct my_object* obj)
{
    lws_mutex_destroy(obj->mutex);
}
```

#### 嵌入到结构体 - 使用栈分配

```c
struct my_object {
    lws_mutex_t mutex;  // 嵌入式
    int data;
};

void init_object(struct my_object* obj)
{
    lws_mutex_init(&obj->mutex);
}

void cleanup_object(struct my_object* obj)
{
    lws_mutex_cleanup(&obj->mutex);
}
```

### 总结

这个优化实现了真正的"零成本抽象"：
- ✅ 无性能损失
- ✅ 无额外内存开销
- ✅ 保持 API 简洁性
- ✅ 保持跨平台兼容性
- ✅ 提供使用灵活性

代码更改最小，但收益巨大！🎉
