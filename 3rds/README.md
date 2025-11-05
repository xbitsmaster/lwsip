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

### 2. mbedtls - Cryptographic Library
- **Repository**: https://github.com/Mbed-TLS/mbedtls
- **Version**: v4.0.0
- **License**: Apache 2.0
- **Purpose**: SSL/TLS and cryptographic library for embedded systems
- **Features**:
  - SSL/TLS v1.2 and v1.3 protocols
  - DTLS (Datagram TLS)
  - X.509 certificate handling (parsing, verification, creation)
  - Cryptographic algorithms (AES, RSA, ECC, ChaCha20, etc.)
  - Hash functions (SHA-256, SHA-512, etc.)
  - Message Authentication Codes (HMAC, CMAC)
  - Key exchange (ECDHE, DHE, RSA)
  - Random number generation (CTR-DRBG, HMAC-DRBG)
  - Small footprint and modular design
- **Use Case**: Enable secure SIP (SIPS), SRTP, and TLS transport for lwsip
  - Secure SIP signaling over TLS
  - SRTP for encrypted media streams
  - Certificate-based authentication
  - End-to-end encryption

### 3. media-server
- **Repository**: https://github.com/ireader/media-server
- **Purpose**: Media streaming protocols support
- **Features**:
  - SIP (Session Initiation Protocol)
  - RTP/RTCP (Real-time Transport Protocol)
  - RTSP (Real-Time Streaming Protocol)
  - SDP (Session Description Protocol)
- **Use Case**: Core SIP and media transport functionality

### 4. sdk
- **Repository**: https://github.com/ireader/sdk
- **Purpose**: Basic SDK utilities
- **Features**:
  - AIO (Asynchronous I/O)
  - HTTP client/server
  - ICE (Interactive Connectivity Establishment)
  - Atomic operations
  - System utilities
- **Use Case**: Low-level networking and system utilities

### 5. avcodec
- **Repository**: https://github.com/ireader/avcodec
- **Purpose**: Audio/Video codec support
- **Features**:
  - H.264/H.265 video codecs
  - Audio codecs (AAC, Opus, etc.)
  - Codec negotiation
- **Use Case**: Media encoding/decoding for RTP streams

### 6. pjsip
- **Repository**: https://github.com/pjsip/pjproject
- **Purpose**: Reference SIP implementation (optional)
- **Features**:
  - Full SIP stack
  - Media transport
  - NAT traversal
- **Use Case**: Alternative SIP stack for comparison

### 7. 3rd
- **Repository**: https://github.com/ireader/3rd
- **Purpose**: Additional third-party utilities
- **Use Case**: Common utilities and helpers

## Build Integration

### CMake Integration
These libraries are included in the main CMakeLists.txt:
```cmake
set(COMMON_INCLUDES
    ${CMAKE_SOURCE_DIR}/3rds/lwip/src/include
    ${CMAKE_SOURCE_DIR}/3rds/mbedtls/include
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
