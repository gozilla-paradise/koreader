#!/usr/bin/env bash
#
# Build a signed Android APK of KOReader on macOS.
#
# Prerequisites (one-time):
#   brew install openjdk@17 flock gnu-tar gnu-getopt make coreutils libdatrie
#   brew install --cask android-commandlinetools
#   ln -sf /opt/homebrew/bin/trietool /opt/homebrew/bin/trietool-0.2
#   sdkmanager --sdk_root=/opt/homebrew/share/android-commandlinetools \
#       "platform-tools" "platforms;android-30" "build-tools;34.0.0" \
#       "ndk;23.2.8568313"
#
# A keystore at ./koreader-debug.keystore is expected. Generate one with:
#   keytool -genkeypair -v -keystore koreader-debug.keystore \
#           -storepass koreader -alias koreader -keyalg RSA -keysize 2048 \
#           -validity 10000 -keypass koreader \
#           -dname "CN=KOReader Local, O=Local, C=US"
#
# Usage:  scripts/build-android.sh [arm|arm64|x86|x86_64]   (default: arm64)

set -euo pipefail

ARCH="${1:-arm64}"

export JAVA_HOME=/opt/homebrew/opt/openjdk@17
export ANDROID_HOME=/opt/homebrew/share/android-commandlinetools
export ANDROID_SDK_ROOT=$ANDROID_HOME
export ANDROID_NDK_HOME=$ANDROID_HOME/ndk/23.2.8568313
export ANDROID_NDK_ROOT=$ANDROID_NDK_HOME
export NDKABI=28
export SDKROOT=$(xcrun --show-sdk-path)
export PATH="$JAVA_HOME/bin:/opt/homebrew/opt/coreutils/libexec/gnubin:/opt/homebrew/opt/gnu-getopt/bin:/opt/homebrew/opt/make/libexec/gnubin:$PATH"

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

./kodev release "android-$ARCH"

APK=$(ls -1t "koreader-android-${ARCH}-v"*.apk | head -1)
if [[ -z "$APK" ]]; then
    echo "No APK found after build." >&2
    exit 1
fi

KEYSTORE=koreader-debug.keystore
if [[ ! -f "$KEYSTORE" ]]; then
    echo "Keystore $KEYSTORE missing. See header of this script to generate one." >&2
    exit 1
fi

BUILD_TOOLS="$ANDROID_HOME/build-tools/34.0.0"
cp "$APK" "$APK.unsigned"
"$BUILD_TOOLS/zipalign" -f 4 "$APK.unsigned" "$APK"
"$BUILD_TOOLS/apksigner" sign \
    --ks "$KEYSTORE" --ks-pass pass:koreader \
    --key-pass pass:koreader --ks-key-alias koreader "$APK"
rm -f "$APK.unsigned"

echo
echo "Built and signed: $APK"
echo "Install with:     adb install -r \"$APK\""
