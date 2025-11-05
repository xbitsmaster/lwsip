# Third-Party Dependencies

This directory contains all third-party libraries used by lwsip.

## Libraries

### 1. lwip - Lightweight IP Stack
- **Repository**: https://github.com/lwip-tcpip/lwip
- **Version**: STABLE-2_2_1_RELEASE
- **License**: BSD License
- **Purpose**: TCP/IP protocol stack for embedded systems
- **Features**:
  - Full TCP/IP protocol suite (IPv4/IPv6)
  - UDP, TCP, ICMP, IGMP protocols
  - Network interfaces (Ethernet, PPP, etc.)
  - Socket API and Raw/Native API
  - DHCP, DNS, SNMP support
- **Use Case**: Provides network stack for RTOS targets without OS TCP/IP

### 2. media-server
- **Repository**: https://github.com/ireader/media-server
- **Purpose**: Media streaming protocols support
- **Features**:
  - SIP (Session Initiation Protocol)
  - RTP/RTCP (Real-time Transport Protocol)
  - RTSP (Real-Time Streaming Protocol)
  - SDP (Session Description Protocol)
- **Use Case**: Core SIP and media transport functionality

### 3. sdk
- **Repository**: https://github.com/ireader/sdk
- **Purpose**: Basic SDK utilities
- **Features**:
  - AIO (Asynchronous I/O)
  - HTTP client/server
  - ICE (Interactive Connectivity Establishment)
  - Atomic operations
  - System utilities
- **Use Case**: Low-level networking and system utilities

### 4. avcodec
- **Repository**: https://github.com/ireader/avcodec
- **Purpose**: Audio/Video codec support
- **Features**:
  - H.264/H.265 video codecs
  - Audio codecs (AAC, Opus, etc.)
  - Codec negotiation
- **Use Case**: Media encoding/decoding for RTP streams

### 5. pjsip
- **Repository**: https://github.com/pjsip/pjproject
- **Purpose**: Reference SIP implementation (optional)
- **Features**:
  - Full SIP stack
  - Media transport
  - NAT traversal
- **Use Case**: Alternative SIP stack for comparison

### 6. 3rd
- **Repository**: https://github.com/ireader/3rd
- **Purpose**: Additional third-party utilities
- **Use Case**: Common utilities and helpers

## Build Integration

### CMake Integration
These libraries are included in the main CMakeLists.txt:
```cmake
set(COMMON_INCLUDES
    ${CMAKE_SOURCE_DIR}/3rds/lwip/src/include
    ${CMAKE_SOURCE_DIR}/3rds/media-server/libsip/include
    ${CMAKE_SOURCE_DIR}/3rds/sdk/include
    # ... other includes
)
```

### Makefile Integration
The main Makefile builds these dependencies in order:
```makefile
make -C 3rds/sdk
make -C 3rds/media-server
# lwip will be built as needed
```

## Adding New Dependencies

To add a new third-party library:

1. Clone the repository:
   ```bash
   git clone <repo-url> 3rds/<library-name>
   ```

2. Add to git:
   ```bash
   git add 3rds/<library-name>
   git commit -m "feat: add <library-name> to 3rds"
   ```

3. Update CMakeLists.txt with include paths and libraries

4. Update this README with library information

## Notes

- All libraries in this directory are maintained as embedded git repositories
- Use `git submodule` for better management if needed
- Check each library's LICENSE file for usage terms
- Version information is tracked via git commit hashes
