#!/bin/bash
# test-plugin-manual.sh - Launch OBS for manual plugin testing
# This script prepares the plugin and gives you instructions to load it manually

set -e

BUILD_DIR="build"
PLUGIN_NAME="libobs-canon-eos.so"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== OBS Canon EOS Plugin Manual Test ===${NC}"
echo ""

# Check if plugin was built
if [ ! -f "$BUILD_DIR/$PLUGIN_NAME" ]; then
    echo -e "${RED}ERROR: Plugin not found at $BUILD_DIR/$PLUGIN_NAME${NC}"
    echo "Build it first with: cd build && cmake .. && make"
    exit 1
fi

PLUGIN_PATH="$(pwd)/$BUILD_DIR/$PLUGIN_NAME"
PLUGIN_SIZE=$(du -h "$PLUGIN_PATH" | cut -f1)

echo -e "${GREEN}✓ Plugin ready for testing${NC}"
echo "  Location: $PLUGIN_PATH"
echo "  Size: $PLUGIN_SIZE"
echo ""

echo -e "${CYAN}=== MANUAL LOADING INSTRUCTIONS ===${NC}"
echo ""
echo "1. OBS will launch in a moment"
echo ""
echo "2. Go to: ${YELLOW}Tools → Scripts${NC}"
echo ""
echo "3. Click the ${YELLOW}+ (Plus)${NC} button"
echo ""
echo "4. Navigate to and select:"
echo "   ${CYAN}$PLUGIN_PATH${NC}"
echo ""
echo "5. OR, add a source:"
echo "   ${YELLOW}Sources → Add (+) → Canon EOS Camera${NC}"
echo ""
echo "6. Watch the terminal for any error messages"
echo ""

echo -e "${YELLOW}Press Enter to launch OBS...${NC}"
read

echo -e "${GREEN}Launching OBS...${NC}"
echo ""

# Launch OBS with verbose logging (filter out spammy audio warnings)
OBS_VERBOSE=1 obs --verbose 2>&1 | grep -v "TS_SMOOTHING_THRESHOLD\|Audio timestamp" | tee obs-manual-test.log

echo ""
echo -e "${GREEN}=== Test Complete ===${NC}"
echo "Log saved to: $(pwd)/obs-manual-test.log"
echo ""
echo "If OBS crashed, check the log for:"
echo "  grep -i 'canon\|segfault\|error' obs-manual-test.log"
