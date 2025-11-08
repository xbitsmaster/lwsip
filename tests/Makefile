CC = gcc
CFLAGS = -Wall -Wextra -O0 -g -D__LWS_PTHREAD__ -I../include -I../osal/include -I../3rds/media-server/libsip/include -I../3rds/sdk/include -I../3rds/sdk/libhttp/include
LDFLAGS = -lpthread

# libsip library path
LIBSIP_DIR = ../3rds/media-server/libsip
LIBSIP_LIB = $(LIBSIP_DIR)/debug.darwin/libsip.a

# SDK library path
SDK_DIR = ../3rds/sdk/libsdk
SDK_LIB = $(SDK_DIR)/debug.darwin/libsdk.a

# HTTP library path
HTTP_DIR = ../3rds/sdk/libhttp
HTTP_LIB = $(HTTP_DIR)/debug.darwin/libhttp.a

# DEBUG_SIP flag (uncomment to enable SIP layer stub testing)
# DEBUG_SIP = 1

# Add DEBUG_SIP to CFLAGS if enabled
ifdef DEBUG_SIP
    CFLAGS += -DDEBUG_SIP
endif

# Source files - Transport layer
LWSIP_SRC = ../src/lwsip.c
TRANS_COMMON_SRC = ../src/lws_trans.c
TRANS_UDP_SRC = ../src/lws_trans_udp.c
OSAL_MEM_SRC = ../osal/src/macos/lws_mem.c
OSAL_MUTEX_SRC = ../osal/src/macos/lws_mutex.c
OSAL_THREAD_SRC = ../osal/src/macos/lws_thread.c
TEST_SRC = lws_trans_test.c

# Source files - SIP layer
AGENT_SRC = ../src/lws_agent.c
TIMER_SRC = ../src/lws_timer.c
AGENT_TEST_SRC = lws_agent_test_main.c

# Source files - Stub
SESS_STUB_SRC = sess_stub.c

# Source files - Caller and Callee
CALLER_SRC = caller.c
CALLEE_SRC = callee.c

# Source files - Stub (conditional for old tests)
ifdef DEBUG_SIP
    SESS_STUB_OLD_SRC = lws_sess_stub.c
endif

# Object files - Transport layer test
TRANS_OBJS = lwsip.o lws_trans.o lws_trans_udp.o lws_mem.o lws_trans_test.o

# Object files - SIP layer test
ifdef DEBUG_SIP
    SIP_OBJS = lws_mem.o lws_timer.o lws_agent_test_main.o
endif

# Object files - Caller and Callee (shared components)
# Note: No sess_stub.o - simplified version without media session
SHARED_OBJS = lws_trans.o lws_trans_udp.o lws_agent.o lws_timer.o lws_mem.o lws_mutex.o lws_thread.o

# Object files - Caller
CALLER_OBJS = $(SHARED_OBJS) caller.o

# Object files - Callee
CALLEE_OBJS = $(SHARED_OBJS) callee.o

# Target executables
TARGET_TRANS = lws_trans_test
TARGET_SIP_FAKE = sip_fake
TARGET_CALLER = caller
TARGET_CALLEE = callee
ifdef DEBUG_SIP
    TARGET_SIP = lws_agent_test
endif

all: $(TARGET_TRANS) $(TARGET_SIP_FAKE) $(TARGET_CALLER) $(TARGET_CALLEE)
ifdef DEBUG_SIP
all: $(TARGET_SIP)
endif

# Transport layer test
$(TARGET_TRANS): $(TRANS_OBJS)
	$(CC) $(TRANS_OBJS) $(LDFLAGS) -o $(TARGET_TRANS)
	@echo "Build successful! Run with: ./$(TARGET_TRANS)"

# SIP layer test (only when DEBUG_SIP is enabled)
ifdef DEBUG_SIP
$(TARGET_SIP): $(SIP_OBJS) $(LIBSIP_LIB) $(SDK_LIB) $(HTTP_LIB)
	$(CC) $(SIP_OBJS) $(LIBSIP_LIB) $(HTTP_LIB) $(SDK_LIB) $(LDFLAGS) -o $(TARGET_SIP)
	@echo "Build successful! Run with: ./$(TARGET_SIP)"
endif

# SIP fake server
$(TARGET_SIP_FAKE): sip_fake.c
	$(CC) $(CFLAGS) sip_fake.c -o $(TARGET_SIP_FAKE)
	@echo "Build successful! Run with: ./$(TARGET_SIP_FAKE)"

# Caller test
$(TARGET_CALLER): $(CALLER_OBJS) $(LIBSIP_LIB) $(SDK_LIB) $(HTTP_LIB)
	$(CC) $(CALLER_OBJS) $(LIBSIP_LIB) $(HTTP_LIB) $(SDK_LIB) $(LDFLAGS) -o $(TARGET_CALLER)
	@echo "Build successful! Run with: ./$(TARGET_CALLER)"

# Callee test
$(TARGET_CALLEE): $(CALLEE_OBJS) $(LIBSIP_LIB) $(SDK_LIB) $(HTTP_LIB)
	$(CC) $(CALLEE_OBJS) $(LIBSIP_LIB) $(HTTP_LIB) $(SDK_LIB) $(LDFLAGS) -o $(TARGET_CALLEE)
	@echo "Build successful! Run with: ./$(TARGET_CALLEE)"

# Object file compilation rules
lwsip.o: $(LWSIP_SRC)
	$(CC) $(CFLAGS) -c $(LWSIP_SRC) -o lwsip.o

lws_trans.o: $(TRANS_COMMON_SRC)
	$(CC) $(CFLAGS) -c $(TRANS_COMMON_SRC) -o lws_trans.o

lws_trans_udp.o: $(TRANS_UDP_SRC)
	$(CC) $(CFLAGS) -c $(TRANS_UDP_SRC) -o lws_trans_udp.o

lws_mem.o: $(OSAL_MEM_SRC)
	$(CC) $(CFLAGS) -c $(OSAL_MEM_SRC) -o lws_mem.o

lws_mutex.o: $(OSAL_MUTEX_SRC)
	$(CC) $(CFLAGS) -c $(OSAL_MUTEX_SRC) -o lws_mutex.o

lws_thread.o: $(OSAL_THREAD_SRC)
	$(CC) $(CFLAGS) -c $(OSAL_THREAD_SRC) -o lws_thread.o

lws_trans_test.o: $(TEST_SRC)
	$(CC) $(CFLAGS) -c $(TEST_SRC) -o lws_trans_test.o

# SIP layer object files
lws_agent.o: $(AGENT_SRC)
	$(CC) $(CFLAGS) -c $(AGENT_SRC) -o lws_agent.o

lws_timer.o: $(TIMER_SRC)
	$(CC) $(CFLAGS) -c $(TIMER_SRC) -o lws_timer.o

lws_agent_test_main.o: $(AGENT_TEST_SRC)
	$(CC) $(CFLAGS) -c $(AGENT_TEST_SRC) -o lws_agent_test_main.o

# Media session stub
sess_stub.o: $(SESS_STUB_SRC)
	$(CC) $(CFLAGS) -c $(SESS_STUB_SRC) -o sess_stub.o

# Caller and Callee object files
caller.o: $(CALLER_SRC)
	$(CC) $(CFLAGS) -c $(CALLER_SRC) -o caller.o

callee.o: $(CALLEE_SRC)
	$(CC) $(CFLAGS) -c $(CALLEE_SRC) -o callee.o

ifdef DEBUG_SIP
lws_sess_stub.o: $(SESS_STUB_OLD_SRC)
	$(CC) $(CFLAGS) -c $(SESS_STUB_OLD_SRC) -o lws_sess_stub.o
endif

# Build libsip if needed
$(LIBSIP_LIB):
	@echo "Building libsip..."
	@cd $(LIBSIP_DIR) && $(MAKE)

# Build SDK library if needed
$(SDK_LIB):
	@echo "Building SDK library..."
	@cd $(SDK_DIR) && $(MAKE)

# Build HTTP library if needed
$(HTTP_LIB):
	@echo "Building HTTP library..."
	@cd $(HTTP_DIR) && $(MAKE)

clean:
	rm -f *.o $(TARGET_TRANS) $(TARGET_SIP_FAKE) $(TARGET_CALLER) $(TARGET_CALLEE)
ifdef DEBUG_SIP
	rm -f $(TARGET_SIP)
endif

test: $(TARGET_TRANS)
	@echo "========================================"
	@echo "Running transport layer tests..."
	@echo "========================================"
	./$(TARGET_TRANS)

ifdef DEBUG_SIP
test: $(TARGET_SIP)
	@echo ""
	@echo "========================================"
	@echo "Running SIP layer tests..."
	@echo "========================================"
	./$(TARGET_SIP)
endif

# Build with DEBUG_SIP enabled
debug_sip:
	@echo "========================================"
	@echo "Building with DEBUG_SIP enabled..."
	@echo "========================================"
	$(MAKE) DEBUG_SIP=1

.PHONY: all clean test debug_sip
