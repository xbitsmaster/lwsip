# lwsip - Lightweight SIP Stack for RTOS

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Linux%20%7C%20macOS%20%7C%20RTOS-lightgrey.svg)](README.md)

**[中文版](docs/README_zh.md)** | English

Lightweight SIP agent protocol stack designed for embedded systems and RTOS.

## Features

- ✅ **Complete SIP UAC Functions**: REGISTER, INVITE, BYE, CANCEL, UNREGISTER
- ✅ **RTP/RTCP Media Transport**: Support for audio and video streams
- ✅ **ICE NAT Traversal**: Integrated STUN/TURN support
- ✅ **Flexible Transport Layer**: Extensible transports including TCP/UDP, MQTT
- ✅ **Cross-Platform Abstraction**: Based on OSAL, supports Linux/macOS/RTOS
- ✅ **Worker Thread Support**: Optional background event processing
- ✅ **Zero-Copy Design**: Efficient media data handling
- ✅ **Modular Architecture**: Clear separation of SIP/RTP/Media layers

## Quick Start

### Dependencies

- GCC/Clang compiler
- CMake 3.10+
- pthread library (Linux/macOS)
- Third-party libraries (included in 3rds directory):
  - [media-server](https://github.com/ireader/media-server) - SIP/RTP protocol implementation
  - [sdk](https://github.com/ireader/sdk) - Basic SDK tools
  - [avcodec](https://github.com/ireader/avcodec) - Audio/video codecs
  - [lwip](https://github.com/lwip-tcpip/lwip) - TCP/IP protocol stack (optional)
  - [mbedtls](https://github.com/Mbed-TLS/mbedtls) - TLS/encryption support (optional)

### Build

```bash
# 1. Clone the repository
git clone <repository-url>
cd lwsip

# 2. Build (using script)
./build.sh

# 3. Or build manually
mkdir -p build && cd build
cmake ..
make

# Build outputs
# - Static library: build/lib/liblwsip.a
# - CLI tool: build/bin/lwsip-cli
```

### Clean

```bash
./clean.sh
```

## Project Structure

```
lwsip/
├── include/              # Public headers
│   ├── lws_agent.h     # SIP agent core interface
│   ├── lws_uac.h        # User Agent Client
│   ├── lws_uas.h        # User Agent Server
│   ├── lws_session.h    # RTP session management
│   ├── lws_payload.h    # RTP payload encapsulation
│   ├── lws_media.h      # Media source/sink abstraction
│   ├── lws_transport.h  # Transport layer abstraction
│   ├── lws_ice.h        # ICE NAT traversal
│   ├── lws_types.h      # Type definitions
│   └── lws_error.h      # Error code definitions
│
├── src/                 # Implementation files
│   ├── lws_agent.c     # SIP agent implementation
│   ├── lws_uac.c        # UAC implementation
│   ├── lws_uas.c        # UAS implementation
│   ├── lws_session.c    # RTP session implementation
│   ├── lws_payload.c    # Payload encapsulation
│   ├── lws_media.c      # Media I/O implementation
│   ├── lws_transport_tcp.c  # TCP/UDP transport
│   ├── lws_transport_mqtt.c # MQTT transport
│   ├── lws_ice.c        # ICE implementation
│   ├── lws_sip_timer.c  # SIP timer
│   └── lws_error.c      # Error code mapping
│
├── cmd/                 # Command-line tools
│   └── lwsip_cli.c      # CLI test tool
│
├── osal/                # OS Abstraction Layer
│   ├── include/         # OSAL headers
│   └── src/             # Platform-specific implementations
│       ├── linux/       # Linux implementation
│       └── macos/       # macOS implementation
│
├── 3rds/                # Third-party dependencies
│   ├── media-server/    # SIP/RTP protocol stack
│   ├── sdk/             # Basic SDK
│   ├── avcodec/         # Audio/video codecs
│   ├── lwip/            # TCP/IP protocol stack
│   └── mbedtls/         # TLS/crypto library
│
├── media/               # Test media files
├── scripts/             # Helper scripts
├── build.sh             # Build script
├── clean.sh             # Clean script
└── CMakeLists.txt       # CMake configuration
```

## Usage Examples

### 1. Create SIP Agent

```c
#include "lws_agent.h"

// Configure client
lws_config_t config = {
    .server_host = "192.168.1.100",
    .server_port = 5060,
    .local_port = 5080,
    .username = "1002",
    .password = "1234",
    .realm = "asterisk",
    .enable_audio = 1,
    .enable_video = 0,
    .audio_codec = LWS_AUDIO_CODEC_PCMU,
    .use_worker_thread = 1,  // Use background thread
};

// Setup callbacks
lws_agent_handler_t handler = {
    .on_reg_state = on_reg_state,
    .on_call_state = on_call_state,
    .on_incoming_call = on_incoming_call,
    .on_error = on_error,
    .param = NULL,
};

// Create client
lws_agent_t* agent = lws_agent_create(&config, &handler);
if (!client) {
    fprintf(stderr, "Failed to create client\n");
    return -1;
}

// Start client
lws_agent_start(client);
```

### 2. Register to SIP Server

```c
// Initiate registration
int ret = lws_uac_register(client);
if (ret != 0) {
    fprintf(stderr, "Register failed: %s\n", lws_error_string(ret));
}

// Handle registration state in callback
void on_reg_state(void* param, lws_reg_state_t state, int code) {
    switch (state) {
    case LWS_REG_REGISTERED:
        printf("Registered successfully\n");
        break;
    case LWS_REG_UNREGISTERED:
        printf("Unregistered\n");
        break;
    case LWS_REG_FAILED:
        printf("Registration failed: %d\n", code);
        break;
    }
}
```

### 3. Make a Call

```c
// One-line call initiation
lws_session_t* session = lws_call(client, "sip:1001@192.168.1.100");
if (!session) {
    fprintf(stderr, "Failed to initiate call\n");
    return -1;
}

// Handle call state
void on_call_state(void* param, const char* peer, lws_call_state_t state) {
    switch (state) {
    case LWS_CALL_STATE_RINGING:
        printf("Ringing...\n");
        break;
    case LWS_CALL_STATE_CONNECTED:
        printf("Call connected\n");
        break;
    case LWS_CALL_STATE_TERMINATED:
        printf("Call terminated\n");
        break;
    }
}
```

### 4. Answer Incoming Call

```c
void on_incoming_call(void* param, const char* from,
                      const char* to, const char* sdp, int sdp_len) {
    printf("Incoming call from: %s\n", from);

    // Answer the call
    lws_session_t* session = lws_answer(client, from);

    // Or reject
    // lws_uas_reject(client, from, 486);  // Busy Here
}
```

### 5. Hang Up Call

```c
// Hang up current call
int ret = lws_hangup(client, session);
if (ret != 0) {
    fprintf(stderr, "Hangup failed: %s\n", lws_error_string(ret));
}
```

### 6. Event Loop

```c
// Method 1: Manual event loop (without worker thread)
while (running) {
    // Process SIP events (100ms timeout)
    int ret = lws_agent_loop(client, 100);
    if (ret < 0) {
        fprintf(stderr, "Error: %s\n", lws_error_string(ret));
        break;
    }
}

// Method 2: Use worker thread (set use_worker_thread=1 in config)
// Client will automatically process events in background, no need to call lws_agent_loop
```

### 7. Cleanup

```c
// Unregister
lws_uac_unregister(client);

// Stop client
lws_agent_stop(client);

// Destroy client
lws_agent_destroy(client);
```

## CLI Tool Usage

The project provides a command-line test tool `lwsip-cli`:

```bash
# Basic usage
./build/bin/lwsip-cli [options]

# See cmd/README.md for detailed usage
```

## OSAL (OS Abstraction Layer)

lwsip uses OSAL for cross-platform support. See [osal/README.md](osal/README.md) for details.

Supported platforms:
- ✅ Linux (pthread)
- ✅ macOS (pthread + os_unfair_lock)
- 🔄 FreeRTOS (planned)
- 🔄 Zephyr (planned)
- 🔄 RT-Thread (planned)

## API Documentation

### Core API

- `lws_agent_create()` - Create SIP agent
- `lws_agent_start()` - Start client
- `lws_agent_stop()` - Stop client
- `lws_agent_destroy()` - Destroy client
- `lws_agent_loop()` - Event loop (manual mode)

### UAC API

- `lws_uac_register()` - Initiate registration
- `lws_uac_unregister()` - Unregister
- `lws_call()` - Make a call (simplified API)
- `lws_hangup()` - Hang up call (simplified API)

### UAS API

- `lws_answer()` - Answer incoming call (simplified API)
- `lws_uas_reject()` - Reject incoming call

### Session API

- `lws_session_create()` - Create RTP session
- `lws_session_start()` - Start session
- `lws_session_stop()` - Stop session
- `lws_session_destroy()` - Destroy session
- `lws_session_poll()` - Poll RTP/RTCP data
- `lws_session_set_dialog()` - Set SIP dialog
- `lws_session_get_dialog()` - Get SIP dialog

## Error Handling

All APIs return integer error codes. Use `lws_error_string()` to get error descriptions:

```c
int ret = lws_call(client, peer);
if (ret < 0) {
    fprintf(stderr, "Error: %s (0x%08x)\n",
            lws_error_string(ret), ret);
}
```

Error codes are defined in `include/lws_error.h`

## Testing

### Interoperability Test with FreeSWITCH

See `scripts/freeswitch/README.md` for FreeSWITCH test environment setup.

## Development Guide

- Coding style: See `CLAUDE.md`
- Naming convention: All public APIs use `lws_` prefix
- Structure definition: `typedef struct xxxx {} xxxx_t;`
- Header guards: `#ifndef __LWS_XXX_H__`
- Code formatting: Use clang-format

## Contributing

Issues and Pull Requests are welcome.

## License

[MIT License](LICENSE)

## Acknowledgments

This project is based on the following open source projects:
- [media-server](https://github.com/ireader/media-server) - SIP/RTP/RTSP protocol implementation
- [sdk](https://github.com/ireader/sdk) - Basic SDK utilities
- [avcodec](https://github.com/ireader/avcodec) - Audio/video codec library
- [lwip](https://github.com/lwip-tcpip/lwip) - Lightweight TCP/IP stack
- [mbedtls](https://github.com/Mbed-TLS/mbedtls) - TLS/crypto library

## Contact

- Issues: GitHub Issues
- Email: <your-email>

---

**Version**: v1.0.0
**Last Updated**: 2025-11-05
