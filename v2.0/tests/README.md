# lwsip Testing Framework

This directory contains unit and integration tests for the lwsip project.

## Directory Structure

```
tests/
├── unity_framework/    # Unity C testing framework (third-party)
├── mocks/              # Mock implementations of external dependencies
│   ├── mock_sip_agent.h
│   └── mock_sip_agent.c
├── unit/               # Unit tests for individual modules
│   └── test_transport_tcp.c
├── integration/        # Integration tests (end-to-end)
├── fixtures/           # Test data and fixtures
├── CMakeLists.txt      # CMake build configuration
└── README.md           # This file
```

## Testing Framework

We use [Unity](https://github.com/ThrowTheSwitch/Unity) as our C testing framework. Unity is lightweight, portable, and well-suited for embedded systems development.

## Building Tests

### Prerequisites

- CMake 3.10 or higher
- GCC or Clang compiler
- Make

### Build Steps

```bash
# From the project root directory
cd tests
mkdir -p build
cd build
cmake ..
make
```

## Running Tests

### Run All Tests

```bash
# From tests/build directory
make run_tests
```

Or use CTest directly:

```bash
ctest --output-on-failure
```

### Run Individual Tests

```bash
# From tests/build directory
./test_transport_tcp
```

## Writing New Tests

### Unit Test Structure

Create a new test file in `tests/unit/`:

```c
/**
 * @file test_my_module.c
 * @brief Unit tests for my module
 */

#include "../../unity_framework/src/unity.h"
#include "../../src/my_module.c"  // Include source for testing static functions

/* Test Setup and Teardown */

void setUp(void)
{
    // Initialize before each test
}

void tearDown(void)
{
    // Cleanup after each test
}

/* Test Cases */

void test_my_function_should_return_true(void)
{
    TEST_ASSERT_TRUE(my_function());
}

void test_my_function_should_handle_null(void)
{
    TEST_ASSERT_EQUAL(ERROR_CODE, my_function(NULL));
}

/* Main Test Runner */

int main(void)
{
    UNITY_BEGIN();

    RUN_TEST(test_my_function_should_return_true);
    RUN_TEST(test_my_function_should_handle_null);

    return UNITY_END();
}
```

### Adding Tests to CMake

Edit `tests/CMakeLists.txt`:

```cmake
# Add new test executable
add_executable(test_my_module
    unit/test_my_module.c
)

target_link_libraries(test_my_module
    unity
    mocks
)

# Register with CTest
add_test(NAME test_my_module COMMAND test_my_module)
```

## Mock Objects

Mock implementations are provided in the `tests/mocks/` directory. These allow testing components in isolation by replacing external dependencies.

### Using Mocks

```c
#include "../mocks/mock_sip_agent.h"

void test_with_mock(void)
{
    // Reset mock state
    mock_sip_agent_reset();

    // Configure mock behavior
    mock_sip_agent_set_create_result((sip_agent_t*)0x1234);

    // Run code under test
    my_function_that_uses_sip_agent();

    // Verify mock was called
    TEST_ASSERT_EQUAL(1, mock_sip_agent_get_create_count());
}
```

### Creating New Mocks

1. Create header file in `tests/mocks/mock_xxx.h`
2. Create implementation in `tests/mocks/mock_xxx.c`
3. Add to mocks library in `CMakeLists.txt`

## Unity Assertions

Common Unity assertions:

```c
// Basic assertions
TEST_ASSERT(condition)
TEST_ASSERT_TRUE(condition)
TEST_ASSERT_FALSE(condition)

// Equality
TEST_ASSERT_EQUAL(expected, actual)
TEST_ASSERT_EQUAL_INT(expected, actual)
TEST_ASSERT_EQUAL_STRING(expected, actual)

// Pointers
TEST_ASSERT_NULL(pointer)
TEST_ASSERT_NOT_NULL(pointer)
TEST_ASSERT_EQUAL_PTR(expected, actual)

// Memory
TEST_ASSERT_EQUAL_MEMORY(expected, actual, len)
```

## Test Naming Conventions

- Test files: `test_<module_name>.c`
- Test functions: `test_<function>_should_<expected_behavior>`
- Example: `test_tcp_transport_create_should_allocate_memory`

## Continuous Integration

Tests can be integrated into CI/CD pipelines:

```bash
# Example CI script
cd tests
mkdir build && cd build
cmake ..
make
ctest --output-on-failure
```

## Best Practices

1. **Isolation**: Each test should be independent and not rely on other tests
2. **Clarity**: Test names should clearly describe what is being tested
3. **Coverage**: Aim for high code coverage, especially for critical paths
4. **Mocking**: Use mocks to isolate the component under test
5. **Edge Cases**: Test boundary conditions, null pointers, error cases
6. **Documentation**: Comment complex test setups

## Troubleshooting

### Build Errors

- Ensure all source files have correct include paths
- Check that Unity framework was cloned properly
- Verify CMake version compatibility

### Test Failures

- Run tests individually to isolate failures
- Check test setup/teardown functions
- Verify mock configurations
- Use debugger with test executable

## Resources

- [Unity Documentation](https://github.com/ThrowTheSwitch/Unity)
- [Unity Assertions Cheat Sheet](https://github.com/ThrowTheSwitch/Unity/blob/master/docs/UnityAssertionsReference.md)
- [Test-Driven Development Guide](https://github.com/ThrowTheSwitch/Unity/blob/master/docs/UnityGettingStartedGuide.md)
