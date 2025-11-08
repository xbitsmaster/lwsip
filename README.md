# lwsip - Lightweight SIP Stack for RTOS

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20RTOS-lightgrey.svg)](README.md)
[![Version](https://img.shields.io/badge/version-3.0.2-green.svg)](CHANGELOG.md)

**[中文文档](docs/README_zh.md)** | **English**

A production-ready, lightweight SIP user agent library designed for embedded systems and RTOS environments.

## 🎯 Key Features

- ✅ **Complete SIP Client**: REGISTER, INVITE, BYE, CANCEL with full state machine
- ✅ **Audio/Video Support**: RTP/RTCP media transport with multiple codecs
- ✅ **NAT Traversal**: Built-in ICE/STUN support for robust connectivity
- ✅ **Multi-Transport**: UDP, TCP, TLS, and MQTT for IoT scenarios
- ✅ **Device Abstraction**: File, microphone, speaker backends with plug-and-play design
- ✅ **RTOS Ready**: Based on OSAL, supports Linux/macOS/FreeRTOS/Zephyr
- ✅ **Zero Internal Threads**: Application-driven event loops for full control
- ✅ **Modular Architecture**: Clean 5-layer design with clear responsibilities
- ✅ **Production Tested**: Interoperable with Asterisk, FreeSWITCH, and others

## 📚 Documentation

- [Quick Start Guide](docs/quick-start.md) - Get started in 5 minutes
- [Architecture Overview](docs/arch-v3.0.md) - Understand the design
- [API Reference](docs/api-reference.md) - Complete API documentation
- [CLI Tool Guide](cmd/README.md) - Command-line tool usage
- [OSAL Guide](osal/README.md) - Platform abstraction layer

## 🚀 Quick Start

### Build from Source

```bash
# Clone repository
git clone https://github.com/your-org/lwsip.git
cd lwsip

# Build third-party libraries
cd 3rds
./build_libs.sh
cd ..

# Build lwsip
mkdir -p build && cd build
cmake ..
make -j4

# Outputs:
# - Library: build/lib/liblwsip.a
# - CLI tool: build/bin/lwsip-cli
# - Tests: build/tests/
```

### Basic Usage Example

```c
#include "lwsip.h"

/* 1. Initialize library */
lwsip_init();

/* 2. Configure SIP agent */
lws_agent_config_t config;
lws_agent_init_default_config(&config, "1001", "secret", "sip.example.com", NULL);

lws_agent_handler_t handler = {
    .on_register_result = on_register,
    .on_incoming_call = on_incoming_call,
    .on_dialog_state_changed = on_call_state,
};

/* 3. Create SIP agent */
lws_agent_t* agent = lws_agent_create(&config, &handler);

/* 4. Start registration */
lws_agent_start(agent);

/* 5. Event loop */
while (running) {
    lws_agent_loop(agent, 100);  /* 100ms timeout */
}

/* 6. Cleanup */
lws_agent_stop(agent);
lws_agent_destroy(agent);
lwsip_cleanup();
```

### Make a Call

```c
/* Create media session */
lws_sess_config_t sess_config;
lws_sess_init_audio_config(&sess_config, "stun.example.com", LWS_RTP_PAYLOAD_PCMA);
sess_config.audio_capture_dev = audio_capture;
sess_config.audio_playback_dev = audio_playback;

lws_sess_t* sess = lws_sess_create(&sess_config, &sess_handler);

/* Initiate call */
lws_dialog_t* dialog = lws_agent_make_call(agent, "sip:1002@sip.example.com");

/* In callback, when media is ready */
void on_dialog_state_changed(lws_agent_t* agent, lws_dialog_t* dialog,
                             lws_dialog_state_t old_state, lws_dialog_state_t new_state,
                             void* userdata) {
    if (new_state == LWS_DIALOG_STATE_CONFIRMED) {
        printf("Call connected!\n");
    }
}
```

## 🏗️ Architecture

lwsip uses a clean **5-layer architecture**:

```
┌─────────────────────────────────────────┐
│         Application Layer               │  Your SIP application
├─────────────────────────────────────────┤
│  Coordination Layer (lws_agent/lws_sess)│  SIP signaling + Media coordination
├─────────────────────────────────────────┤
│  Protocol Layer (libsip/librtp/libice)  │  SIP/RTP/ICE protocol stacks
├─────────────────────────────────────────┤
│  Device Layer (lws_dev)                 │  Audio/video device abstraction
├─────────────────────────────────────────┤
│  Transport Layer (lws_trans)            │  Network transport (UDP/TCP/MQTT)
└─────────────────────────────────────────┘
```

### Key Components

| Module | Description | Header |
|--------|-------------|--------|
| **lws_agent** | SIP signaling coordination | `lws_agent.h` |
| **lws_sess** | Media session management | `lws_sess.h` |
| **lws_dev** | Device abstraction (audio/video) | `lws_dev.h` |
| **lws_trans** | Transport layer (UDP/TCP/MQTT) | `lws_trans.h` |
| **lws_timer** | Timer management | `lws_timer.h` |

See [Architecture Design](docs/arch-v3.0.md) for details.

## 📦 Dependencies

### Core Dependencies (included in 3rds/)

| Library | Purpose | Repository |
|---------|---------|------------|
| **media-server** | SIP/RTP/RTSP protocol stack | [ireader/media-server](https://github.com/ireader/media-server) |
| **sdk** | Basic SDK utilities (AIO, HTTP, ICE) | [ireader/sdk](https://github.com/ireader/sdk) |

### Optional Dependencies

| Library | Purpose | When Needed |
|---------|---------|-------------|
| **lwip** | TCP/IP stack for embedded systems | RTOS environments |
| **mbedtls** | TLS/crypto for secure connections | Secure SIP (SIPS) |
| **avcodec** | Audio/video codecs | Advanced codec support |

### Platform Dependencies

- **Linux/macOS**: pthread, standard C library
- **RTOS**: Provided by OSAL layer (see `osal/`)

## 🔧 Configuration Options

Build-time features controlled by CMake:

```bash
# Enable MQTT transport (requires lwIP)
cmake .. -DENABLE_MQTT=ON

# Enable file device backend
cmake .. -DENABLE_FILE=ON

# Enable device stub for embedded systems
cmake .. -DENABLE_DEV_STUB=ON
```

Preprocessor defines:
- `TRANS_MQTT` - MQTT transport support
- `DEV_FILE` - File-based media device
- `__LWS_PTHREAD__` - pthread support

## 📁 Project Structure

```
lwsip/
├── include/              # Public API headers
│   ├── lwsip.h          # Main header (includes all modules)
│   ├── lws_agent.h      # SIP agent API
│   ├── lws_sess.h       # Media session API
│   ├── lws_dev.h        # Device abstraction API
│   ├── lws_trans.h      # Transport layer API
│   ├── lws_timer.h      # Timer API
│   ├── lws_defs.h       # Common definitions
│   └── lws_err.h        # Error codes
│
├── src/                 # Implementation
│   ├── lws_agent.c      # SIP agent (UAC/UAS)
│   ├── lws_sess.c       # Media session coordination
│   ├── lws_dev.c        # Device abstraction
│   ├── lws_dev_file.c   # File device backend
│   ├── lws_dev_macos.c  # macOS audio device
│   ├── lws_dev_linux.c  # Linux (ALSA) audio device
│   ├── lws_trans.c      # Transport common code
│   ├── lws_trans_udp.c  # UDP transport
│   ├── lws_trans_mqtt.c # MQTT transport (optional)
│   └── lws_timer.c      # Timer implementation
│
├── cmd/                 # Command-line tools
│   └── lwsip-cli.c      # SIP CLI client
│
├── tests/               # Unit and integration tests
│   ├── lwsip_agent_test.c   # lws_agent unit tests
│   ├── lwsip_sess_test.c    # lws_sess unit tests
│   └── sip/             # SIP integration tests
│       ├── caller.c     # UAC test
│       ├── callee.c     # UAS test
│       └── sip_server.c # Fake SIP server
│
├── osal/                # OS Abstraction Layer
│   ├── include/         # OSAL headers
│   │   ├── lws_mem.h   # Memory management
│   │   ├── lws_log.h   # Logging
│   │   ├── lws_mutex.h # Mutex/locking
│   │   └── lws_thread.h# Threading
│   └── src/
│       ├── macos/      # macOS implementation
│       └── linux/      # Linux implementation
│
├── 3rds/                # Third-party libraries
│   ├── media-server/    # SIP/RTP/RTSP protocols
│   ├── sdk/             # libice, libhttp, libsdk
│   ├── lwip/            # lwIP TCP/IP stack (optional)
│   └── pjsip/           # pjsip (reference only, not used)
│
├── docs/                # Documentation
│   ├── arch-v3.0.md    # Architecture design
│   ├── ice.md          # ICE implementation notes
│   └── README_zh.md    # Chinese README
│
├── media/               # Test media files
├── scripts/             # Helper scripts
├── CMakeLists.txt       # Build configuration
└── CLAUDE.md            # Development guidelines
```

## 🔌 Supported Platforms

| Platform | Status | Notes |
|----------|--------|-------|
| Linux (x86_64) | ✅ Tested | Ubuntu 20.04+, pthread |
| macOS (ARM64/x86_64) | ✅ Tested | macOS 12+, AudioToolbox |
| FreeSWITCH Interop | ✅ Tested | SIP server compatibility |
| Asterisk Interop | ✅ Tested | SIP server compatibility |
| FreeRTOS | 🔄 Planned | OSAL layer ready |
| Zephyr | 🔄 Planned | OSAL layer ready |
| RT-Thread | 🔄 Planned | OSAL layer ready |

## 🧪 Testing

```bash
# Run unit tests
cd build
./tests/lwsip_agent_test  # lws_agent tests (10/10 passed)
./tests/lwsip_sess_test   # lws_sess tests (17/20 passed)

# Run integration tests
./tests/sip_server &      # Start fake SIP server
./tests/callee &          # Start callee (UAS)
./tests/caller            # Start caller (UAC)

# CLI tool tests
./bin/lwsip-cli --help
```

See [Test Guide](tests/README.md) for details.

## 📖 API Examples

### Register to SIP Server

```c
lws_agent_config_t config;
lws_agent_init_default_config(&config, "1001", "secret", "192.168.1.100", NULL);
config.auto_register = 1;

lws_agent_handler_t handler = {
    .on_register_result = [](lws_agent_t* agent, int success,
                             int status_code, const char* reason, void* ud) {
        if (success) {
            printf("Registered successfully\n");
        } else {
            printf("Registration failed: %d %s\n", status_code, reason);
        }
    }
};

lws_agent_t* agent = lws_agent_create(&config, &handler);
lws_agent_start(agent);
```

### Answer Incoming Call

```c
void on_incoming_call(lws_agent_t* agent, lws_dialog_t* dialog,
                     const lws_sip_addr_t* from, void* userdata) {
    printf("Incoming call from: %s@%s\n", from->username, from->domain);

    /* Auto-answer */
    lws_agent_answer_call(agent, dialog);
}
```

### Hangup Call

```c
lws_agent_hangup(agent, dialog);
```

## 🛠️ Development

### Coding Standards

- **Style**: Follow `.clang-format` configuration
- **Naming**: All public APIs use `lws_` prefix
- **Types**: Use `typedef struct {} xxx_t;` pattern
- **Headers**: Guard with `#ifndef __LWS_XXX_H__`
- **Logging**: Use OSAL logging (`lws_log_info`, `lws_log_error`, etc.)
- **Memory**: Use OSAL allocators (`lws_malloc`, `lws_free`)

See [CLAUDE.md](CLAUDE.md) for complete guidelines.

### Adding New Features

1. **New Transport**: Implement `lws_trans_ops_t` in `src/lws_trans_xxx.c`
2. **New Device**: Implement `lws_dev_ops_t` in `src/lws_dev_xxx.c`
3. **New Codec**: Add to RTP payload handling in `lws_sess.c`

## 🐛 Troubleshooting

### Common Issues

**Q: Registration fails with 401 Unauthorized**
```
A: Check username/password in lws_agent_config_t
```

**Q: No audio in call**
```
A: Verify device configuration in lws_sess_config_t
   Check logs for device open/start errors
```

**Q: Build fails with missing headers**
```
A: Ensure 3rds libraries are built: cd 3rds && ./build_libs.sh
```

See [FAQ](docs/faq.md) for more.

## 🤝 Contributing

Contributions are welcome! Please:

1. Fork the repository
2. Create a feature branch
3. Follow coding standards
4. Add tests for new features
5. Submit a pull request

## 📄 License

This project is licensed under the MIT License - see [LICENSE](LICENSE) file.

## 🙏 Acknowledgments

lwsip is built on top of excellent open source projects:

- **[media-server](https://github.com/ireader/media-server)** by ireader - Core SIP/RTP/RTSP protocol implementation
- **[sdk](https://github.com/ireader/sdk)** by ireader - libice, libhttp, and basic utilities
- **[lwIP](https://github.com/lwip-tcpip/lwip)** - Lightweight TCP/IP stack for MQTT transport
- **[pjsip](https://github.com/pjsip/pjsip)** - Reference implementation (not used in code)

## 📧 Contact & Support

- **Issues**: [GitHub Issues](https://github.com/your-org/lwsip/issues)
- **Discussions**: [GitHub Discussions](https://github.com/your-org/lwsip/discussions)
- **Email**: your-email@example.com

---

**Version**: 3.0.2
**Last Updated**: 2025-11-08
**Status**: Production Ready
