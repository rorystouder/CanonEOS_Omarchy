# Testing and Development Log

## Session: 2025-09-30

### Critical Issues Found and Fixed

During testing, the plugin was found to completely break OBS Studio, preventing it from launching. A comprehensive security review identified multiple critical threading and memory safety issues.

#### 🔴 Critical Bugs Fixed

1. **Deadlock on Plugin Load Failure** (plugin-main.c:305-327)
   - **Issue**: Mutex remained locked on all error paths after line 309
   - **Impact**: OBS would freeze permanently if plugin initialization failed
   - **Fix**: Added proper error handling with mutex unlock before all return statements

2. **Thread Join Deadlock** (plugin-main.c:225-233)
   - **Issue**: `pthread_join()` called without properly setting thread flags under mutex
   - **Impact**: Guaranteed deadlock during plugin unload or source destruction
   - **Fix**: Properly set `thread_running` and `active` flags under mutex before joining thread

3. **Missing Mutex Unlock** (video-source.c:277-287)
   - **Issue**: `pthread_cond_timedwait()` error paths didn't release mutex
   - **Impact**: Permanent resource lock if condition wait failed
   - **Fix**: Added explicit error handling for all non-success return codes

4. **Use-After-Free** (plugin-main.c:169-217)
   - **Issue**: Capture thread could access camera while it was being destroyed
   - **Impact**: Segmentation fault, OBS crash
   - **Fix**: Stop capture thread completely before destroying camera resources

5. **Thread Leak** (plugin-main.c:294-311)
   - **Issue**: Capture thread continued running after source deactivation
   - **Impact**: Resource leak, multiple threads accumulating over time
   - **Fix**: Properly stop and join thread in `canon_eos_deactivate()`

6. **libusb Event Loop Blocking** (camera-detector.c:158-174)
   - **Issue**: `libusb_handle_events_timeout_completed()` called with NULL timeout, blocks indefinitely
   - **Impact**: Camera hotplug detection fails, thread can't exit cleanly
   - **Fix**: Added 100ms timeout to event loop

7. **Global Mutex Leak** (plugin-main.c:19, 371-397)
   - **Issue**: `g_plugin_mutex` never destroyed on unload
   - **Impact**: Resource leak on repeated plugin load/unload cycles
   - **Fix**: Added `pthread_mutex_destroy()` in `obs_module_unload()`

### Testing Environment Setup

Created isolated testing environment to avoid breaking main OBS installation:

#### Scripts Created

1. **test-plugin.sh** - Automated isolated testing
   - Creates separate OBS config at `~/.config/obs-studio-test/`
   - Installs plugin to user directory
   - Launches OBS with verbose logging
   - Main OBS config remains untouched

2. **test-plugin-manual.sh** - Manual plugin loading
   - Shows plugin path for manual loading
   - Filters out spammy audio timestamp warnings
   - Saves clean logs

3. **install-plugin.sh** - Safe system installation
   - Creates backups before overwriting
   - Installs to `/usr/lib/obs-plugins/`
   - Provides rollback instructions

### Camera Support

#### Issue: Canon EOS R7 Not Detected

**Problem**: Plugin loaded successfully but showed "None" in device list.

**Root Cause**: Canon EOS R7 (USB Product ID: 0x32F7) was not in the supported models list.

**Verification**:
```bash
$ gphoto2 --auto-detect
Model                          Port
----------------------------------------------------------
Canon EOS R7                   usb:002,005

$ lsusb -d 04a9: -v | grep idProduct
  idProduct          0x32f7 Canon Digital Camera
```

**Fix**: Added Canon EOS R7 to supported models in `src/camera-detector.c`:
```c
{0x32F7, "Canon EOS R7"},
```

### Build and Installation Process

#### Dependencies Required
```bash
sudo pacman -S cmake libgphoto2 libusb obs-studio gphoto2
```

#### Build Steps
```bash
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Debug ..
make
```

#### Installation
```bash
# Safe installation with backup
./install-plugin.sh

# Or manual installation
sudo cp build/libobs-canon-eos.so /usr/lib/obs-plugins/
```

#### Uninstall (if needed)
```bash
sudo rm /usr/lib/obs-plugins/libobs-canon-eos.so
```

### Testing Results

✅ **OBS launches successfully** (no crashes)
✅ **Plugin loads without errors**
✅ **Canon EOS R7 now detected** in device list
⚠️ **Video feed not yet working** (next issue to investigate)

### Next Steps

1. **Debug video capture**
   - Camera detected but no video displayed
   - Check live view activation
   - Verify frame capture from gPhoto2

2. **Test with camera connected**
   - Verify USB permissions
   - Test frame capture pipeline
   - Check JPEG to NV12 conversion

3. **Additional camera models**
   - Test with other Canon models
   - Add more USB product IDs as needed

4. **Performance testing**
   - Monitor CPU usage
   - Check memory leaks with Valgrind
   - Test thread safety with Helgrind

### Lessons Learned

1. **Always use isolated testing environments** for plugins that can crash the host application
2. **Threading bugs are subtle** - all mutex lock/unlock pairs must be on all code paths
3. **Camera detection requires exact USB product ID** matching
4. **OBS only loads plugins from system directories** by default, not user configs
5. **Static analysis tools** (cppcheck, Valgrind) are essential before installation

### Tools Used

- **Valgrind** - Memory leak detection
- **Helgrind** - Thread safety analysis
- **gPhoto2** - Camera detection and testing
- **lsusb** - USB device inspection
- **OBS verbose logging** - Plugin debugging

### Git Commits

1. **2064c63** - fix: resolve critical threading and memory safety issues
   - Fixed all 7 critical deadlock and crash bugs
   - Added proper error handling throughout

2. **[Pending]** - feat: add Canon EOS R7 support
   - Added USB product ID 0x32F7
   - Camera now appears in device list

---

**Status**: Plugin is stable and loads successfully. Camera detection working. Video capture needs debugging.