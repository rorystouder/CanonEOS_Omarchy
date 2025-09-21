# Development Guidelines for Canon EOS OBS Plugin

## Project Overview
This is a native C/C++ plugin for OBS Studio that enables Canon EOS cameras as video sources on Linux systems. The plugin interfaces with cameras via gPhoto2/libusb and integrates with OBS through its plugin API.

## Code Style & Standards

### Language: C with C++ where necessary
- Use C99 standard for core plugin code
- C++11 for OBS API integration where required
- Follow OBS Studio coding conventions for consistency

### Naming Conventions
```c
// Functions: snake_case
canon_camera_init()
video_source_create()

// Structs: snake_case with _t suffix
typedef struct canon_camera_data_t {
    // Members: snake_case
    char *device_name;
    int connection_status;
} canon_camera_data_t;

// Constants: UPPER_SNAKE_CASE
#define MAX_CAMERAS 4
#define DEFAULT_FRAMERATE 30

// Enums: PascalCase for type, UPPER_SNAKE_CASE for values
typedef enum CanonModel {
    CANON_EOS_5D_MARK_IV,
    CANON_EOS_R5
} CanonModel;
```

### Memory Management
- ALWAYS pair malloc/free, new/delete
- Initialize pointers to NULL
- Check all allocations for failure
- Use RAII patterns in C++ sections
- Prefer stack allocation when possible
- Clean up resources in reverse order of acquisition

```c
// Good practice
canon_camera_data_t *data = calloc(1, sizeof(canon_camera_data_t));
if (!data) {
    blog(LOG_ERROR, "Failed to allocate camera data");
    return NULL;
}
// ... use data ...
free(data);
data = NULL;
```

### Error Handling
- Never ignore return values
- Use consistent error codes
- Log errors with context
- Fail gracefully, never crash OBS
- Provide user-friendly error messages

```c
typedef enum {
    CANON_SUCCESS = 0,
    CANON_ERROR_NO_DEVICE = -1,
    CANON_ERROR_USB_INIT = -2,
    CANON_ERROR_MEMORY = -3
} canon_error_t;
```

## Architecture Guidelines

### Thread Safety
- Use mutex locks for all shared camera data
- Avoid blocking the OBS main thread
- Perform USB operations in worker threads
- Use atomic operations for status flags

### Camera Interface Layer
- Abstract gPhoto2/libusb behind clean interface
- Support mock cameras for testing
- Handle hot-plug events properly
- Implement exponential backoff for reconnection

### Video Pipeline
- Zero-copy frame passing when possible
- Use OBS native formats (NV12, I420, RGBA)
- Implement frame rate limiting
- Handle resolution changes gracefully

## Testing Requirements

### Before Every Commit
Run these commands:
```bash
# Build tests
make test

# Memory checks
valgrind --leak-check=full ./test_canon_plugin

# Static analysis
cppcheck --enable=all src/

# Format check
clang-format -i src/*.c src/*.h
```

### Test Coverage
- Unit tests for all utility functions
- Integration tests for camera detection
- Mock camera for CI/CD testing
- Stress tests for long-running sessions

## Performance Guidelines

### Memory Usage
- Target: < 200MB per camera
- Pool video buffers, don't allocate per frame
- Release unused resources promptly
- Monitor for memory leaks with Valgrind

### CPU Usage
- Target: < 15% on modern CPUs
- Use hardware acceleration when available
- Optimize hot paths with profiling data
- Avoid unnecessary memory copies

### Latency
- Target: < 100ms end-to-end
- Minimize buffering in pipeline
- Use async I/O for USB operations
- Profile with `perf` tool regularly

## Security Considerations

### USB Device Access
- Validate all USB device descriptors
- Sanitize data from camera
- Run with minimal privileges
- Never trust device-provided sizes

### Input Validation
```c
// Always validate
if (width <= 0 || width > 8192 || height <= 0 || height > 4320) {
    blog(LOG_WARNING, "Invalid resolution: %dx%d", width, height);
    return CANON_ERROR_INVALID_PARAM;
}
```

## OBS Integration Best Practices

### Plugin Lifecycle
```c
// 1. Register plugin
OBS_DECLARE_MODULE()

// 2. Initialize resources
bool obs_module_load(void) {
    // Register sources
    // Initialize USB subsystem
    return true;
}

// 3. Cleanup properly
void obs_module_unload(void) {
    // Stop all cameras
    // Release USB resources
    // Free global data
}
```

### Source Implementation
- Implement all required callbacks
- Handle properties changes atomically
- Support OBS Studio filters
- Provide meaningful defaults

## Logging Guidelines

```c
// Use OBS blog function with appropriate levels
blog(LOG_INFO, "[Canon-EOS] Camera connected: %s", model_name);
blog(LOG_ERROR, "[Canon-EOS] USB error: %s", libusb_strerror(ret));
blog(LOG_DEBUG, "[Canon-EOS] Frame %d captured", frame_count);

// Include context in log messages
// Use consistent prefix [Canon-EOS]
// Don't log in performance-critical paths
```

## Build System

### CMake Requirements
```cmake
cmake_minimum_required(VERSION 3.16)
project(obs-canon-eos VERSION 1.0.0)

# Use modern CMake practices
set(CMAKE_C_STANDARD 99)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Enable all warnings
add_compile_options(-Wall -Wextra -Wpedantic)
```

### Dependencies
- Check versions at configure time
- Provide clear error messages for missing deps
- Support both system and bundled libraries
- Use pkg-config when available

## Documentation

### Code Documentation
```c
/**
 * @brief Initialize Canon camera connection
 * @param device_path USB device path (e.g., "/dev/bus/usb/001/002")
 * @param config Camera configuration structure
 * @return CANON_SUCCESS on success, error code otherwise
 */
canon_error_t canon_camera_init(const char *device_path,
                                const canon_config_t *config);
```

### User Documentation
- Maintain README.md with setup instructions
- Document supported camera models
- Provide troubleshooting guide
- Include example configurations

## Version Control

### Commit Messages
```
feat: Add support for Canon EOS R5
fix: Resolve USB disconnection crash
perf: Optimize frame buffer allocation
docs: Update supported camera list
test: Add integration tests for 4K capture
refactor: Extract USB handling to separate module
```

### Branch Strategy
- `main`: Stable releases only
- `develop`: Integration branch
- `feature/*`: New features
- `fix/*`: Bug fixes
- `release/*`: Release preparation

## Debugging Tools

### Recommended Tools
```bash
# Memory debugging
valgrind --tool=memcheck ./obs

# Performance profiling
perf record -g ./obs
perf report

# USB debugging
usbmon / wireshark with USBPcap

# GDB with OBS symbols
gdb ./obs -ex "set follow-fork-mode child"
```

### Debug Builds
```cmake
# Enable debug symbols
set(CMAKE_BUILD_TYPE Debug)
add_compile_options(-g3 -O0)
add_definitions(-DDEBUG_MODE)
```

## Common Pitfalls to Avoid

1. **Don't block OBS main thread** - Use worker threads for USB I/O
2. **Don't leak file descriptors** - Always close USB devices
3. **Don't assume camera capabilities** - Query and validate
4. **Don't ignore partial USB transfers** - Handle short reads
5. **Don't hardcode paths** - Use OBS config APIs
6. **Don't poll unnecessarily** - Use event-driven design
7. **Don't trust user input** - Validate all parameters

## Platform-Specific Notes

### Linux Kernel Compatibility
- Test with kernel 5.x and 6.x
- Handle udev events properly
- Respect USB autosuspend settings
- Work with SELinux/AppArmor policies

### Omarchy Linux Specifics
- Follow Arch packaging guidelines
- Test with latest rolling release
- Support AUR installation
- Handle systemd integration

## Release Checklist

- [ ] All tests pass
- [ ] No memory leaks (Valgrind clean)
- [ ] No compiler warnings
- [ ] Performance targets met
- [ ] Documentation updated
- [ ] Changelog updated
- [ ] Version bumped
- [ ] Package builds successfully
- [ ] Tested on fresh Omarchy install

## Continuous Integration

```yaml
# CI Pipeline should:
- Build with multiple compiler versions
- Run unit and integration tests
- Check code formatting
- Perform static analysis
- Test with mock camera
- Generate coverage reports
- Build packages for distribution
```

## Support Matrix

### Minimum Requirements
- OBS Studio: 27.0+
- gPhoto2: 2.5.27+
- libusb: 1.0.24+
- CMake: 3.16+
- GCC: 9+ / Clang: 10+

### Tested Configurations
Document all tested combinations of:
- Camera models
- OBS versions
- Linux distributions
- Kernel versions
- USB controllers

## Code Review Guidelines

Before submitting PR:
- [ ] Code follows style guide
- [ ] Tests added for new features
- [ ] No regression in existing tests
- [ ] Memory usage acceptable
- [ ] CPU usage within target
- [ ] Documentation updated
- [ ] Commit messages follow convention
- [ ] No security vulnerabilities
- [ ] Error handling comprehensive
- [ ] Logging appropriate

---
*Last Updated: 2025-09-21*
*Version: 1.0*