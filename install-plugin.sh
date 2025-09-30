#!/bin/bash
# install-plugin.sh - Safely install Canon EOS plugin to system

set -e

PLUGIN_NAME="libobs-canon-eos.so"
BUILD_DIR="build"
OBS_PLUGIN_DIR="/usr/lib/obs-plugins"
BACKUP_DIR="$HOME/.local/share/obs-canon-eos-backups"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

echo -e "${GREEN}=== Canon EOS Plugin Safe Installation ===${NC}"
echo ""

# Check if plugin exists
if [ ! -f "$BUILD_DIR/$PLUGIN_NAME" ]; then
    echo -e "${RED}ERROR: Plugin not built${NC}"
    echo "Run: cd build && cmake .. && make"
    exit 1
fi

# Check if OBS plugin directory exists
if [ ! -d "$OBS_PLUGIN_DIR" ]; then
    echo -e "${RED}ERROR: OBS plugin directory not found: $OBS_PLUGIN_DIR${NC}"
    exit 1
fi

INSTALL_PATH="$OBS_PLUGIN_DIR/$PLUGIN_NAME"

# Create backup if plugin already exists
if [ -f "$INSTALL_PATH" ]; then
    echo -e "${YELLOW}Existing plugin found, creating backup...${NC}"
    mkdir -p "$BACKUP_DIR"
    TIMESTAMP=$(date +%Y%m%d_%H%M%S)
    cp "$INSTALL_PATH" "$BACKUP_DIR/${PLUGIN_NAME}.backup.$TIMESTAMP"
    echo -e "${GREEN}✓ Backup saved to: $BACKUP_DIR/${PLUGIN_NAME}.backup.$TIMESTAMP${NC}"
    echo ""
fi

echo -e "${YELLOW}Installing plugin to: $INSTALL_PATH${NC}"
echo "This requires sudo password..."
echo ""

sudo cp "$BUILD_DIR/$PLUGIN_NAME" "$INSTALL_PATH"
sudo chmod 755 "$INSTALL_PATH"

if [ -f "$INSTALL_PATH" ]; then
    echo -e "${GREEN}✓ Plugin installed successfully${NC}"
    echo ""
    echo -e "${GREEN}=== Next Steps ===${NC}"
    echo "1. Launch OBS: obs"
    echo "2. Add source: Sources → Add (+) → Canon EOS Camera"
    echo "3. Select your camera from the dropdown"
    echo ""
    echo -e "${YELLOW}If OBS crashes or won't start:${NC}"
    echo "  sudo rm $INSTALL_PATH"
    echo ""
    echo -e "${YELLOW}To restore backup:${NC}"
    echo "  ls -la $BACKUP_DIR"
else
    echo -e "${RED}ERROR: Installation failed${NC}"
    exit 1
fi
