#!/bin/bash
# test-plugin.sh - Safe OBS plugin testing environment
# This script creates an isolated OBS configuration to test the Canon EOS plugin
# without affecting your main OBS Studio installation.

set -e

# Configuration
TEST_CONFIG="$HOME/.config/obs-studio-test"
PLUGIN_DIR="$TEST_CONFIG/plugins/obs-canon-eos"
BUILD_DIR="build"
PLUGIN_NAME="libobs-canon-eos.so"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== OBS Canon EOS Plugin Test Environment ===${NC}"
echo ""

# Check if OBS is installed
if ! command -v obs &> /dev/null; then
    echo -e "${RED}ERROR: OBS Studio is not installed${NC}"
    echo "Install with: sudo pacman -S obs-studio"
    exit 1
fi

# Check if build directory exists
if [ ! -d "$BUILD_DIR" ]; then
    echo -e "${YELLOW}Build directory not found. Creating and building...${NC}"
    mkdir -p "$BUILD_DIR"
    cd "$BUILD_DIR"
    cmake -DCMAKE_BUILD_TYPE=Debug ..
    make
    cd ..
fi

# Check if plugin was built
if [ ! -f "$BUILD_DIR/$PLUGIN_NAME" ]; then
    echo -e "${YELLOW}Plugin not built. Building now...${NC}"
    cd "$BUILD_DIR"
    make
    cd ..
fi

if [ ! -f "$BUILD_DIR/$PLUGIN_NAME" ]; then
    echo -e "${RED}ERROR: Failed to build plugin${NC}"
    exit 1
fi

# Create test config directory structure
echo -e "${GREEN}Setting up isolated test environment...${NC}"
mkdir -p "$PLUGIN_DIR/bin/64bit"
mkdir -p "$TEST_CONFIG/basic/profiles"
mkdir -p "$TEST_CONFIG/basic/scenes"

# Copy plugin to test location
echo -e "${GREEN}Installing plugin to test environment...${NC}"
cp "$BUILD_DIR/$PLUGIN_NAME" "$PLUGIN_DIR/bin/64bit/"

# Check if plugin copied successfully
if [ ! -f "$PLUGIN_DIR/bin/64bit/$PLUGIN_NAME" ]; then
    echo -e "${RED}ERROR: Failed to copy plugin${NC}"
    exit 1
fi

# Get plugin info
PLUGIN_SIZE=$(du -h "$PLUGIN_DIR/bin/64bit/$PLUGIN_NAME" | cut -f1)

echo ""
echo -e "${GREEN}✓ Test environment ready${NC}"
echo "  Plugin location: $PLUGIN_DIR/bin/64bit/$PLUGIN_NAME"
echo "  Plugin size: $PLUGIN_SIZE"
echo "  Config directory: $TEST_CONFIG"
echo ""
echo -e "${YELLOW}IMPORTANT:${NC}"
echo "  - This is an ISOLATED test environment"
echo "  - Your main OBS config is NOT affected"
echo "  - If OBS crashes, just close this script"
echo ""
echo -e "${GREEN}Camera Permissions:${NC}"
echo "  - Make sure your user is in 'video' group"
echo "  - Check USB permissions with: ls -l /dev/bus/usb/*/*"
echo ""

# Ask user if they want to launch OBS (auto-launch if --auto flag)
if [[ "$1" == "--auto" ]]; then
    REPLY="y"
else
    read -p "Launch OBS with test environment? [Y/n] " -n 1 -r
    echo
fi
if [[ ! $REPLY =~ ^[Nn]$ ]]; then
    echo -e "${GREEN}Launching OBS Studio...${NC}"
    echo -e "${YELLOW}(OBS will use isolated config at: $TEST_CONFIG)${NC}"
    echo ""

    # Launch OBS with test config and verbose logging
    OBS_VERBOSE=1 obs --collection "Canon-EOS-Test" \
                      --profile "Test" \
                      --config "$TEST_CONFIG" \
                      --verbose 2>&1 | tee "$TEST_CONFIG/obs-test.log" &

    OBS_PID=$!

    echo -e "${GREEN}✓ OBS launched (PID: $OBS_PID)${NC}"
    echo "  Log file: $TEST_CONFIG/obs-test.log"
    echo ""
    echo "To check the Canon EOS plugin:"
    echo "  1. Go to Sources → Add (+) → Canon EOS Camera"
    echo "  2. Check if your camera appears in the device list"
    echo ""

    # Wait for OBS to exit
    wait $OBS_PID
    EXIT_CODE=$?

    echo ""
    if [ $EXIT_CODE -eq 0 ]; then
        echo -e "${GREEN}✓ OBS exited cleanly${NC}"
    else
        echo -e "${RED}✗ OBS exited with error code: $EXIT_CODE${NC}"
        echo "Check log: $TEST_CONFIG/obs-test.log"
    fi
fi

echo ""
echo -e "${GREEN}=== Cleanup Options ===${NC}"
echo "To remove test environment:"
echo "  rm -rf $TEST_CONFIG"
echo ""
echo "To test again:"
echo "  ./test-plugin.sh"
echo ""
echo "To install plugin system-wide (ONLY if testing succeeds):"
echo "  cd build && sudo make install"