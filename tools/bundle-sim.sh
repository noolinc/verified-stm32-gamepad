#!/usr/bin/env bash
# Build GamepadSim.app.
#   tools/bundle-sim.sh
#       ad-hoc signed, no restricted entitlement: runs anywhere, the virtual
#       HID device is unavailable (the app says so and keeps working).
#   SIGN_ID="Apple Development: …" PROFILE=path/to/profile.provisionprofile \
#   BUNDLE_ID=com.example.gamepadsim tools/bundle-sim.sh
#       signs with sim/GamepadSim.entitlements and embeds the profile, which
#       must grant com.apple.developer.hid.virtual.device for BUNDLE_ID.
set -euo pipefail
cd "$(dirname "$0")/.."
swift build -c release --product GamepadSim
app=build/GamepadSim.app
rm -rf "$app"
mkdir -p "$app/Contents/MacOS"
cp "$(swift build -c release --show-bin-path)/GamepadSim" "$app/Contents/MacOS/GamepadSim"
cat > "$app/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleExecutable</key><string>GamepadSim</string>
  <key>CFBundleIdentifier</key><string>${BUNDLE_ID:-dev.local.gamepadsim}</string>
  <key>CFBundleName</key><string>Gamepad Simulator</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>0.1.0</string>
  <key>LSMinimumSystemVersion</key><string>15.0</string>
  <key>NSHighResolutionCapable</key><true/>
</dict></plist>
PLIST
if [ -n "${SIGN_ID:-}" ]; then
  [ -f "${PROFILE:-}" ] || { echo "PROFILE must point to a provisioning profile" >&2; exit 2; }
  cp "$PROFILE" "$app/Contents/embedded.provisionprofile"
  codesign --force --options runtime --entitlements sim/GamepadSim.entitlements -s "$SIGN_ID" "$app"
  echo "Signed with the HID virtual-device entitlement: $app"
else
  codesign --force -s - "$app"
  echo "Ad-hoc signed (no virtual HID device): $app"
fi
