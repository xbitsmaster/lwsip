# lws_dev.c 详细实现计划

## 1. 概述

实现设备抽象层 (lws_dev)，提供统一的音视频设备和文件接口。

### 1.1 目标
- 文件接口：使用 libmov 实现 MP4 文件读写
- 音频格式：仅支持 PCM (G.711 alaw/ulaw)
- 设备接口：支持 macOS、Linux、嵌入式平台
- 架构模式：虚拟函数表 (ops) 实现平台特定功能

### 1.2 模块划分
```
src/
├── lws_dev.c              # 核心设备抽象层（公共接口）
├── lws_dev_file.c         # 文件后端（MP4，基于 libmov）
├── lws_dev_macos.c        # macOS 设备后端（AudioQueue API）
├── lws_dev_linux.c        # Linux 设备后端（ALSA）
└── lws_dev_stub.c         # 嵌入式设备后端（用户实现）
```

## 2. 数据结构设计

### 2.1 核心结构体 (lws_dev.c)

```c
struct lws_dev_t {
    lws_dev_type_t type;           // 设备类型
    lws_dev_ops_t* ops;            // 操作函数表
    void* platform_data;           // 平台特定数据

    // 配置
    lws_dev_config_t config;

    // 状态
    lws_dev_state_t state;
    int is_capturing;
    int is_playing;

    // 缓冲区
    uint8_t* buffer;
    size_t buffer_size;
    size_t buffer_used;
};
```

### 2.2 文件后端结构体 (lws_dev_file.c)

```c
typedef struct {
    // libmov writer
    void* mov_writer;              // mov_writer_t*
    int audio_track_id;

    // libmov reader
    void* mov_reader;              // mov_reader_t*
    struct mov_reader_trackinfo_t audio_track_info;

    // 文件信息
    char filepath[256];
    int is_writing;                // 写模式还是读模式

    // PCM 参数
    int sample_rate;
    int channels;
    int bits_per_sample;

    // 时间戳
    uint64_t start_time_ms;
    uint64_t current_pts;
    uint32_t samples_written;
} lws_dev_file_data_t;
```

### 2.3 macOS 设备后端结构体 (lws_dev_macos.c)

```c
#ifdef __APPLE__
typedef struct {
    AudioQueueRef audio_queue;
    AudioQueueBufferRef buffers[3];

    // 音频格式
    AudioStreamBasicDescription format;

    // 回调数据
    lws_dev_t* dev;

    // 同步
    pthread_mutex_t mutex;
} lws_dev_macos_data_t;
#endif
```

### 2.4 Linux 设备后端结构体 (lws_dev_linux.c)

```c
#ifdef __linux__
typedef struct {
    snd_pcm_t* pcm_handle;
    snd_pcm_hw_params_t* hw_params;

    // 参数
    unsigned int sample_rate;
    unsigned int channels;

    // 缓冲区
    snd_pcm_uframes_t frames;
    size_t frame_size;
} lws_dev_linux_data_t;
#endif
```

## 3. 实现步骤

### 步骤 1: 实现 lws_dev.c 核心层

**文件**: src/lws_dev.c

**功能**:
- `lws_dev_create()`: 根据配置创建设备实例，选择合适的 ops
- `lws_dev_destroy()`: 销毁设备实例
- `lws_dev_open()`: 调用 ops->open
- `lws_dev_close()`: 调用 ops->close
- `lws_dev_start()`: 调用 ops->start
- `lws_dev_stop()`: 调用 ops->stop
- `lws_dev_read()`: 调用 ops->read
- `lws_dev_write()`: 调用 ops->write
- 辅助函数：`lws_dev_get_state()`, `lws_dev_get_config()`

**关键点**:
- 根据 `config->backend` 选择正确的 ops 表
- 参数验证
- 状态管理

**预计代码量**: ~300 行

---

### 步骤 2: 实现 lws_dev_file.c 文件后端

**文件**: src/lws_dev_file.c

**功能**:
- 实现 `lws_dev_ops_t` 函数表
- MP4 写入流程:
  1. `file_open()`: 创建 mov_writer，添加音频轨道
  2. `file_start()`: 准备写入
  3. `file_write()`: 写入 PCM 帧到 MP4
  4. `file_stop()`: 完成写入
  5. `file_close()`: 销毁 writer

- MP4 读取流程:
  1. `file_open()`: 创建 mov_reader，获取音频轨道信息
  2. `file_start()`: 准备读取
  3. `file_read()`: 从 MP4 读取 PCM 帧
  4. `file_stop()`: 停止读取
  5. `file_close()`: 销毁 reader

**libmov API 使用**:

```c
// 写入
mov_writer_t* mov_writer_create(...);
int mov_writer_add_audio(mov_writer_t*, &track_info);
int mov_writer_write(mov_writer_t*, track_id, data, bytes, pts, dts, flags);
void mov_writer_destroy(mov_writer_t*);

// 读取
mov_reader_t* mov_reader_create(...);
int mov_reader_getinfo(mov_reader_t*, mov_reader_trackinfo_t*);
int mov_reader_read(mov_reader_t*, buffer, bytes, ...);
void mov_reader_destroy(mov_reader_t*);
```

**PCM 格式**:
- Codec: MOV_OBJECT_G711a (0xFD) 或 MOV_OBJECT_G711u (0xFE)
- 采样率: 8000 Hz (G.711 标准)
- 声道: 1 (单声道)
- 比特率: 8 bits

**预计代码量**: ~500 行

---

### 步骤 3: 实现 lws_dev_macos.c macOS 设备后端

**文件**: src/lws_dev_macos.c

**功能**:
- 使用 AudioQueue API 实现音频采集和播放
- 实现 `lws_dev_ops_t` 函数表
- 音频采集:
  1. `macos_open()`: 创建 AudioQueue 输入队列
  2. `macos_start()`: 开始采集
  3. AudioQueue 回调: 将数据存入 dev->buffer
  4. `macos_read()`: 从 buffer 读取数据

- 音频播放:
  1. `macos_open()`: 创建 AudioQueue 输出队列
  2. `macos_write()`: 将数据写入 buffer
  3. AudioQueue 回调: 从 buffer 获取数据播放
  4. `macos_start()`: 开始播放

**关键 API**:
```c
AudioQueueNewInput/Output()
AudioQueueAllocateBuffer()
AudioQueueEnqueueBuffer()
AudioQueueStart/Stop()
AudioQueueDispose()
```

**预计代码量**: ~400 行

---

### 步骤 4: 实现 lws_dev_linux.c Linux 设备后端

**文件**: src/lws_dev_linux.c

**功能**:
- 使用 ALSA API 实现音频采集和播放
- 实现 `lws_dev_ops_t` 函数表
- 音频采集:
  1. `linux_open()`: 打开 PCM 设备 (hw:0,0)
  2. `linux_start()`: 配置硬件参数，开始采集
  3. `linux_read()`: snd_pcm_readi() 读取数据

- 音频播放:
  1. `linux_open()`: 打开 PCM 设备
  2. `linux_start()`: 配置硬件参数
  3. `linux_write()`: snd_pcm_writei() 写入数据

**关键 API**:
```c
snd_pcm_open()
snd_pcm_hw_params_*()
snd_pcm_prepare()
snd_pcm_readi/writei()
snd_pcm_close()
```

**预计代码量**: ~350 行

---

### 步骤 5: 实现 lws_dev_stub.c 嵌入式设备后端

**文件**: src/lws_dev_stub.c

**功能**:
- 提供桩实现，用户可以根据具体嵌入式平台修改
- 实现 `lws_dev_ops_t` 函数表
- 所有函数返回 `LWSIP_ENOTSUP` (不支持)
- 提供注释说明如何实现

**预计代码量**: ~100 行

---

### 步骤 6: 更新 CMakeLists.txt

**修改文件**:
- CMakeLists.txt
- tests/CMakeLists.txt

**修改内容**:
1. 添加 lws_dev.c 到 LWS_SOURCES
2. 根据平台条件编译:
   ```cmake
   if(APPLE)
       list(APPEND LWS_SOURCES src/lws_dev_macos.c)
       find_library(AUDIOQUEUE_FRAMEWORK AudioToolbox)
       list(APPEND COMMON_LIBS ${AUDIOQUEUE_FRAMEWORK})
   elseif(UNIX)
       list(APPEND LWS_SOURCES src/lws_dev_linux.c)
       find_package(ALSA REQUIRED)
       list(APPEND COMMON_LIBS ${ALSA_LIBRARIES})
   endif()

   # 文件后端总是编译
   list(APPEND LWS_SOURCES src/lws_dev_file.c)

   # 嵌入式桩
   option(ENABLE_DEV_STUB "Enable device stub for embedded" OFF)
   if(ENABLE_DEV_STUB)
       list(APPEND LWS_SOURCES src/lws_dev_stub.c)
   endif()
   ```

3. 添加 libmov 库路径和链接:
   ```cmake
   set(LIB_MOV ${CMAKE_SOURCE_DIR}/3rds/media-server/libmov/${MEDIA_PLATFORM}/libmov.a)
   list(APPEND COMMON_LIBS ${LIB_MOV})
   ```

4. 添加 libmov 头文件路径到 COMMON_INCLUDES

---

### 步骤 7: 编译测试

**测试流程**:
1. 编译 lwsip_static 库
2. 编译 caller 和 callee 测试程序
3. 运行基本功能测试

**预期结果**:
- 编译无错误
- 链接成功（解决之前的 lws_dev_read_audio/write_audio 缺失问题）

---

## 4. 接口映射

### 4.1 lws_dev.h → lws_dev_ops_t 映射

| lws_dev.h API | ops 函数 | 说明 |
|--------------|---------|------|
| lws_dev_create() | - | 创建设备，选择 ops |
| lws_dev_destroy() | - | 销毁设备 |
| lws_dev_open() | ops->open() | 打开设备/文件 |
| lws_dev_close() | ops->close() | 关闭设备/文件 |
| lws_dev_start() | ops->start() | 开始采集/播放 |
| lws_dev_stop() | ops->stop() | 停止采集/播放 |
| lws_dev_read() | ops->read() | 读取数据（采集） |
| lws_dev_write() | ops->write() | 写入数据（播放） |

### 4.2 后端实现函数命名

**文件后端** (lws_dev_file.c):
- `file_open()`, `file_close()`
- `file_start()`, `file_stop()`
- `file_read()`, `file_write()`

**macOS 后端** (lws_dev_macos.c):
- `macos_open()`, `macos_close()`
- `macos_start()`, `macos_stop()`
- `macos_read()`, `macos_write()`

**Linux 后端** (lws_dev_linux.c):
- `linux_open()`, `linux_close()`
- `linux_start()`, `linux_stop()`
- `linux_read()`, `linux_write()`

**嵌入式桩** (lws_dev_stub.c):
- `stub_open()`, `stub_close()`
- `stub_start()`, `stub_stop()`
- `stub_read()`, `stub_write()`

---

## 5. 关键技术点

### 5.1 libmov MP4 写入流程

```c
// 1. 创建 writer
static void* mov_file_buffer_writer_create(const char* filepath) {
    FILE* fp = fopen(filepath, "wb");
    // ... 返回 FILE*
}

static int mov_buffer_write(void* param, uint64_t offset, const void* data, size_t bytes) {
    FILE* fp = (FILE*)param;
    fseek(fp, offset, SEEK_SET);
    return fwrite(data, 1, bytes, fp);
}

mov_writer_t* writer = mov_writer_create(
    mov_file_buffer_writer_create(filepath),
    mov_buffer_write,
    MOV_FLAG_FASTSTART
);

// 2. 添加音频轨道
struct mov_audio_track_info_t audio_info = {
    .object = MOV_OBJECT_G711a,  // or MOV_OBJECT_G711u
    .sample_rate = 8000,
    .channels = 1,
    .bits_per_sample = 8
};
int track_id = mov_writer_add_audio(writer, &audio_info);

// 3. 写入音频帧
int mov_writer_write(writer, track_id, pcm_data, pcm_bytes, pts, dts, 0);

// 4. 销毁 writer
mov_writer_destroy(writer);
```

### 5.2 libmov MP4 读取流程

```c
// 1. 创建 reader
mov_reader_t* reader = mov_reader_create(
    mov_file_buffer_reader_create(filepath),
    mov_buffer_read
);

// 2. 获取轨道信息
struct mov_reader_trackinfo_t track_info;
mov_reader_getinfo(reader, &track_info);

// 3. 读取音频帧
static void on_mov_read(void* param, uint32_t track, const void* buffer,
                        size_t bytes, int64_t pts, int64_t dts, int flags) {
    // 处理读取的 PCM 数据
}

mov_reader_read(reader, buffer, buffer_size, on_mov_read, userdata);

// 4. 销毁 reader
mov_reader_destroy(reader);
```

### 5.3 时间戳计算

```c
// PCM 帧时间戳计算
// samples_per_frame = (sample_rate * frame_duration_ms) / 1000
// 例如: 8000Hz, 20ms => 160 samples

uint64_t pts = (samples_written * 1000) / sample_rate;  // 毫秒
samples_written += samples_per_frame;
```

---

## 6. 测试计划

### 6.1 单元测试

**lws_dev_file_test.c**:
- 写入 PCM 数据到 MP4 文件
- 从 MP4 文件读取 PCM 数据
- 验证数据一致性

**lws_dev_macos_test.c** (仅 macOS):
- 音频采集 5 秒
- 音频播放测试音

**lws_dev_linux_test.c** (仅 Linux):
- 音频采集 5 秒
- 音频播放测试音

### 6.2 集成测试

**caller/callee 测试**:
- 验证 lws_dev 与 lws_sess 集成
- 验证音频采集 → RTP 发送流程
- 验证 RTP 接收 → 音频播放流程

---

## 7. 预计工作量

| 任务 | 预计时间 | 代码量 |
|-----|---------|--------|
| lws_dev.c 核心层 | 1-2 小时 | ~300 行 |
| lws_dev_file.c | 2-3 小时 | ~500 行 |
| lws_dev_macos.c | 2-3 小时 | ~400 行 |
| lws_dev_linux.c | 2-3 小时 | ~350 行 |
| lws_dev_stub.c | 0.5 小时 | ~100 行 |
| CMakeLists 更新 | 0.5 小时 | ~50 行 |
| 编译调试 | 1-2 小时 | - |
| **总计** | **9-15 小时** | **~1700 行** |

---

## 8. 风险与注意事项

### 8.1 风险
1. **libmov API 理解**: MP4 格式复杂，可能需要多次调试
2. **平台差异**: macOS AudioQueue 和 Linux ALSA API 不同
3. **时间戳同步**: PCM 帧时间戳计算需精确

### 8.2 注意事项
1. **错误处理**: 所有 API 调用需检查返回值
2. **资源释放**: 确保 mov_writer/reader 正确销毁
3. **线程安全**: macOS AudioQueue 回调在独立线程
4. **缓冲区大小**: 根据采样率和帧时长计算合适的缓冲区

---

## 9. 下一步行动

1. ✅ 生成实现计划文档（本文档）
2. 实现 lws_dev.c 核心层
3. 实现 lws_dev_file.c 文件后端
4. 实现 lws_dev_macos.c（如果在 macOS）
5. 实现 lws_dev_linux.c（如果在 Linux）
6. 实现 lws_dev_stub.c
7. 更新 CMakeLists.txt
8. 编译测试
9. 调试并修复问题

---

**文档版本**: v1.0
**创建时间**: 2025-11-08
**作者**: Claude Code Assistant
