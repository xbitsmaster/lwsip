# lwsip 测试脚本说明

本目录包含用于测试 SIP 呼叫功能的各种脚本。

## 脚本列表

### 1. pjsua 测试脚本（基于 pjsip 库）

#### `call_remote.sh` - 发起呼叫
使用 pjsua 发起音频呼叫，支持音频播放和录音。

**功能**：
- ✅ 从音频文件播放（media/test_audio_pcmu.wav）
- ✅ 录音到文件（media/call_record.wav）
- ✅ 自动注册、呼叫、挂断
- ✅ 通话时长：15秒

**使用方法**：
```bash
# 基本使用
./call_remote.sh 1000 1001

# 指定服务器
./call_remote.sh 1000 1001 198.19.249.149

# 指定密码
./call_remote.sh 1000 1001 198.19.249.149 5678
```

#### `wait_incoming.sh` - 等待来电
使用 pjsua 等待来电，支持音频播放和录音。

**功能**：
- ✅ 从音频文件播放（media/test_audio_pcmu.wav）
- ✅ 录音到文件（media/wait_record.wav）
- ✅ 自动注册、接听
- ✅ 手动挂断

**使用方法**：
```bash
# 基本使用
./wait_incoming.sh 1000

# 指定服务器
./wait_incoming.sh 1001 198.19.249.149

# 使用不同端口
./wait_incoming.sh 1002 198.19.249.149 1234 5067
```

### 2. lwsip 测试脚本（基于 lwsip 库）

#### `lwsip_call.sh` - 使用 lwsip-cli 发起呼叫

使用 lwsip-cli 发起音频呼叫。

**功能**：
- ✅ 从音频文件播放（media/test_audio_pcmu.wav）
- ❌ 录音到文件（**暂不支持**）
- ✅ 自动注册、呼叫、挂断

**使用方法**：
```bash
# 基本使用（默认：1000 呼叫 1001）
./lwsip_call.sh

# 指定主被叫
./lwsip_call.sh 1000 1002

# 指定服务器
./lwsip_call.sh 1000 1003 198.19.249.149
```

**限制**：
- 当前版本的 lwsip-cli 不支持录音到文件
- 需要修改 `cmd/lwsip-cli.c` 添加 FILE_WRITER 设备支持
- 详见：`lwsip_record_feature.md`

## 音频文件

### 输入文件（播放）

位置：`media/test_audio_pcmu.wav`
- 格式：WAV (PCMU - G.711 μ-law)
- 采样率：8000 Hz
- 声道：单声道
- 大小：78 KB

### 输出文件（录音）

| 脚本 | 录音文件 | 状态 |
|------|----------|------|
| call_remote.sh | media/call_record.wav | ✅ 支持 |
| wait_incoming.sh | media/wait_record.wav | ✅ 支持 |
| lwsip_call.sh | media/lwsip_record.wav | ❌ 暂不支持 |

## 服务器配置

默认 SIP 服务器：`198.19.249.149:5060`

### 已配置用户

| 用户ID | 密码 | 说明 |
|--------|------|------|
| 1000 | 1234 | 测试用户 1 |
| 1001 | 1234 | 测试用户 2 |
| 1002 | 1234 | 测试用户 3 |
| 1003 | 1234 | 测试用户 4 |

## 典型测试场景

### 场景 1：pjsua 双向呼叫测试

**终端 1（被叫 1000）**：
```bash
./wait_incoming.sh 1000
```

**终端 2（主叫 1001）**：
```bash
./call_remote.sh 1001 1000
```

**结果**：
- 1000 接听来电
- 播放音频文件
- 双方录音文件生成

### 场景 2：lwsip-cli 呼叫 pjsua

**终端 1（pjsua 被叫 1001）**：
```bash
./wait_incoming.sh 1001
```

**终端 2（lwsip-cli 主叫 1000）**：
```bash
./lwsip_call.sh 1000 1001
```

**结果**：
- lwsip-cli 发起呼叫
- pjsua 接听并录音
- lwsip-cli 无录音

### 场景 3：lwsip-cli 互相呼叫

**终端 1（等待来电 1001）**：
```bash
./build/bin/lwsip-cli -s sip:198.19.249.149:5060 -u 1001 -p 1234
```

**终端 2（发起呼叫 1000）**：
```bash
./lwsip_call.sh 1000 1001
```

## 对比表

| 功能 | pjsua 脚本 | lwsip-cli 脚本 |
|------|-----------|---------------|
| 音频播放 | ✅ | ✅ |
| 音频录音 | ✅ | ❌ |
| WAV 支持 | ✅ | ✅ |
| MP4 支持 | ✅ | ✅ |
| 自动应答 | ✅ | ✅ |
| 通话时长控制 | ✅ | ✅ |
| 库依赖 | pjsip | lwsip |

## 录音功能实现

lwsip-cli 的录音功能实现方案详见：
- **文档**：`scripts/lwsip_record_feature.md`
- **工作量**：2.5-5 小时
- **难度**：⭐⭐☆☆☆ (容易)
- **可行性**：✅ 完全可行

## 故障排查

### 问题 1：无法连接到服务器

**症状**：
```
[CLI] Failed to register
```

**解决方法**：
1. 检查服务器地址和端口
2. 验证用户名和密码
3. 确认网络连接
4. 检查防火墙设置

### 问题 2：音频文件不存在

**症状**：
```
错误: 播放文件不存在: media/test_audio_pcmu.wav
```

**解决方法**：
```bash
# 检查文件是否存在
ls -lh media/test_audio_pcmu.wav

# 如果不存在，需要准备测试音频文件
```

### 问题 3：lwsip-cli 未编译

**症状**：
```
错误: lwsip-cli 可执行文件不存在
```

**解决方法**：
```bash
cd build
cmake ..
make lwsip-cli
```

## 注意事项

1. **端口冲突**：
   - 多个 SIP 客户端在同一台机器上运行时，需要使用不同的本地端口
   - `wait_incoming.sh` 默认使用 5066 端口
   - `call_remote.sh` 默认使用 5060 端口

2. **音频格式**：
   - 所有脚本都使用 PCMU (G.711 μ-law) 编码
   - 采样率：8000 Hz
   - 声道：单声道

3. **录音文件**：
   - 每次运行前会清理旧的录音文件
   - 录音文件保存在 `media/` 目录

4. **通话时长**：
   - `call_remote.sh`：15秒自动挂断
   - `wait_incoming.sh`：手动挂断（Ctrl+C）
   - `lwsip_call.sh`：自动挂断

## 开发建议

如果要扩展脚本功能，建议：

1. **添加更多参数**：
   - 通话时长
   - 音频编码格式
   - 本地端口选择

2. **支持视频呼叫**：
   - 使用 MP4 视频文件
   - 录制视频流

3. **批量测试**：
   - 自动化测试脚本
   - 并发呼叫测试
   - 压力测试

## 相关文档

- [lwsip-cli 使用说明](../cmd/README.md)
- [lwsip 录音功能实现简报](lwsip_record_feature.md)
- [项目 README](../README.md)

---

**版本**: 1.0
**更新日期**: 2025-11-21
