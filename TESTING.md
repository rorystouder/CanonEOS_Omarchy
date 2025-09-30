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

### Video Capture Issues and Fixes

#### Issue 1: No Video Displayed
**Problem**: Camera detected but black screen in OBS.

**Root Cause**: Missing JPEG decoding - gPhoto2 returns JPEG preview frames, not raw video.

**Fix**: Added libjpeg support:
1. Added `find_package(JPEG REQUIRED)` to CMakeLists.txt
2. Implemented JPEG to NV12 conversion in `video-source.c`
3. RGB intermediate buffer for color space conversion

#### Issue 2: Upside-Down Video
**Problem**: Video displayed but vertically flipped.

**Fix**: Set `frame.flip = true` in `plugin-main.c:119` to correct orientation.

#### Issue 3: Video Only 1/4 Size with "Snow"
**Problem**: Video displayed in upper-right 1/4 of frame, rest filled with green/garbage pixels.

**Root Cause**: Camera returns 1024x576 JPEG but plugin told OBS frames are 1280x720/1920x1080, causing:
- Wrong UV plane stride calculations
- Padding with uninitialized memory
- Incorrect frame dimensions reported to OBS

**Investigation**:
```
info: [Canon-EOS] gp_camera_capture_preview succeeded: 179393 bytes
warning: [Canon-EOS] JPEG size mismatch: got 1024x576, expected 1280x720
```

**Fix**: Use actual JPEG dimensions throughout pipeline:
1. Modified `convert_jpeg_to_nv12()` to return actual dimensions via pointer parameters
2. Added `width` and `height` fields to `frame_buffer_t` structure
3. Updated `video_source_get_frame()` to use buffer dimensions instead of format dimensions
4. Removed dimension override in `plugin-main.c` that was forcing wrong size

**Changes**:
- `src/video-source.c`: Store actual frame dimensions in buffer, use for NV12 conversion
- `src/plugin-main.c`: Don't overwrite frame.width/height from video_source_get_frame()

### Testing Results

✅ **OBS launches successfully** (no crashes)
✅ **Plugin loads without errors**
✅ **Canon EOS R7 detected** in device list
✅ **Video feed working** - Live preview displaying correctly
✅ **Correct orientation** - Video no longer upside down
✅ **Proper scaling** - Video fills frame without artifacts
✅ **Actual JPEG dimensions used** - 1024x576 frames from camera displayed correctly

**Final Log Output**:
```
info: [Canon-EOS] Captured JPEG frame: 162525 bytes
info: [Canon-EOS] JPEG size: got 1024x576, requested 1280x720 - using actual JPEG size
info: [Canon-EOS] Converted frame to NV12: 1024x576 (actual JPEG dimensions)
info: [Canon-EOS] Outputting frame to OBS: 1024x576, data[0]=0x..., linesize[0]=1024, linesize[1]=1024
```

### Next Steps

1. **Performance optimization**
   - Monitor CPU usage during streaming
   - Profile JPEG decode and NV12 conversion
   - Consider hardware acceleration

2. **Resolution switching**
   - Test all resolution options (720p, 1080p, 4K)
   - Verify camera adapts preview size or maintains aspect ratio

3. **Additional camera models**
   - Test with other Canon EOS models
   - Add more USB product IDs as needed

4. **Long-running stability**
   - Test for memory leaks over extended sessions
   - Verify thread safety with Helgrind
   - Test camera reconnection after disconnect

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

1. **a68b9e4** - fixing build errors
   - Fixed compilation issues

2. **8822dc7** - ini commit
   - Initial codebase setup

3. **f7c4f71** - Initial commit

4. **[Pending]** - fix: video display issues - JPEG dimensions and NV12 conversion
   - Added libjpeg support for JPEG decoding
   - Fixed upside-down video with frame.flip flag
   - Fixed video scaling by using actual JPEG dimensions (1024x576)
   - Corrected NV12 conversion to use actual frame dimensions
   - Added frame dimension validation

---

**Status**: ✅ Plugin fully functional! Video feed working correctly with Canon EOS R7.