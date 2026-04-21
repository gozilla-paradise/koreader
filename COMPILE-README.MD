# Building KOReader on macOS

Practical recipes for building KOReader for Android and Kobo from an Apple Silicon Mac. The official KOReader docs recommend a Linux VM; these notes cover what's actually needed to make macOS work end-to-end. Both paths have been exercised in this repo and have helper scripts checked in.

- [Building KOReader on macOS](#building-koreader-on-macos)
  - [Prerequisites (one-time)](#prerequisites-one-time)
  - [Android APK](#android-apk)
    - [First-time SDK/NDK setup](#first-time-sdkndk-setup)
    - [Generate a debug keystore](#generate-a-debug-keystore)
    - [Build](#build)
    - [Install](#install)
  - [Kobo release zip](#kobo-release-zip)
    - [Build](#build-1)
    - [Install](#install-1)
  - [Desktop emulator](#desktop-emulator)
  - [Troubleshooting](#troubleshooting)
    - [Android](#android)
    - [Kobo](#kobo)
  - [Why macOS builds need these patches](#why-macos-builds-need-these-patches)

---

## Prerequisites (one-time)

Homebrew packages used across all targets:

```bash
brew install openjdk@17 flock gnu-tar gnu-getopt make coreutils libdatrie
ln -sf /opt/homebrew/bin/trietool /opt/homebrew/bin/trietool-0.2
```

- `openjdk@17` — Android builds need exactly JDK 17.
- `flock`, `gnu-tar`, `gnu-getopt`, `make`, `coreutils` — KOReader's build scripts assume GNU userland.
- `libdatrie` + the `trietool-0.2` symlink — libthai's dictionary generation invokes `trietool-0.2` on the build host. The cross-compiled `trietool` in the staging dir is ARM-only; our `base/thirdparty/libdatrie/CMakeLists.txt` replaces it with a symlink to the host one for cross builds.

Docker Desktop is required for the Kobo build.

---

## Android APK

### First-time SDK/NDK setup

```bash
brew install --cask android-commandlinetools

export JAVA_HOME=/opt/homebrew/opt/openjdk@17
export ANDROID_HOME=/opt/homebrew/share/android-commandlinetools
export PATH="$JAVA_HOME/bin:$PATH"

# Accept SDK licenses (interactive — hit y/Enter a few times)
yes | sdkmanager --sdk_root="$ANDROID_HOME" --licenses

# Install the exact components KOReader wants
sdkmanager --sdk_root="$ANDROID_HOME" \
    "platform-tools" \
    "platforms;android-30" \
    "build-tools;34.0.0" \
    "ndk;23.2.8568313"
```

### Generate a debug keystore

Gradle doesn't sign the release APK by default; you sign it yourself with a local debug key.

```bash
keytool -genkeypair -v -keystore koreader-debug.keystore \
        -storepass koreader -alias koreader \
        -keyalg RSA -keysize 2048 -validity 10000 \
        -keypass koreader \
        -dname "CN=KOReader Local, O=Local, C=US"
```

### Build

```bash
scripts/build-android.sh            # defaults to arm64
scripts/build-android.sh arm        # or arm, x86, x86_64
```

The script sets `JAVA_HOME`, `ANDROID_HOME`, `ANDROID_NDK_HOME`, `NDKABI=28`, `SDKROOT=$(xcrun --show-sdk-path)`, and prepends `/opt/homebrew/opt/{openjdk@17,coreutils,gnu-getopt,make}/…` to PATH, then runs `./kodev release android-<arch>` and zipalign + apksigner.

Output: `koreader-android-<arch>-v<version>.apk` in the repo root.

### Install

```bash
adb install -r koreader-android-arm64-v<version>.apk
```

---

## Kobo release zip

The Kobo target needs a custom ARM cross-toolchain (matching Kobo's old glibc). Building it natively on macOS is painful; running the whole build inside the official [`koreader/kokobo`](https://hub.docker.com/r/koreader/kokobo) Docker image — which ships the toolchain pre-built — is the sane path.

### Build

Docker must be running. First run will pull a ~4 GB image.

```bash
scripts/build-kobo-in-docker.sh                # defaults to kobov5 (FW 5.x)
scripts/build-kobo-in-docker.sh kobov4         # Mk.7+ on FW 4.x
scripts/build-kobo-in-docker.sh kobo           # broadest compat (all Kobos)
```

The script:
- Launches `koreader/kokobo:latest` with `--platform linux/amd64` (image is amd64-only; runs under qemu on Apple Silicon, 2–3× slower but correct).
- Runs as root inside the container (`--user 0:0`) and marks the mounted repo as `safe.directory` so git works across the UID mismatch that macOS bind mounts cause.
- `apt install libdatrie-dev libdatrie1-bin` so the cross build of libthai can invoke a host `trietool`.
- Pre-populates `base/thirdparty/fbink/build/downloads/source` at the SHA kodev pins, as a workaround for a race in kodev's `clone_git_repo` (parallel ninja jobs hit the same dir and the helper's `flock` isn't seen before the "does the dir exist?" check).
- `./kodev release <variant>`.

Output: `koreader-<variant>-v<version>.zip` and `.targz` in the repo root.

### Install

Connect the Kobo via USB and mount it, then copy the zip's contents into `.adds/koreader/` at the root of the device:

```bash
KOBO=/Volumes/KOBOeReader    # or wherever the device mounts
mkdir -p "$KOBO/.adds/koreader"
unzip -o koreader-kobov5-v<version>.zip -d "$KOBO/.adds/koreader/"
```

Safely eject. Kobo will reprocess; KOReader will appear in the "Beta Features" → external apps list or under the home tile depending on firmware.

---

## Desktop emulator

For quick iteration on Lua / UI work without cross-compiling:

```bash
PATH="/opt/homebrew/opt/gnu-getopt/bin:/opt/homebrew/opt/make/libexec/gnubin:$PATH" \
    ./kodev build

PATH="/opt/homebrew/opt/gnu-getopt/bin:/opt/homebrew/opt/make/libexec/gnubin:$PATH" \
    ./kodev run
```

---

## Troubleshooting

### Android

- **`error loading module 'libs/libkoreader-cre': symbol not found`** — crengine didn't actually include a file you expected. The upstream source list for crengine lives in `base/thirdparty/kpvcrlib/CMakeLists.txt`, not the nested `crengine/crengine/CMakeLists.txt`.
- **`configure: error: No usable iconv()`** — bionic added iconv in API 28. The build uses `NDKABI=28` for this reason.
- **`unknown directive: _archive_entry_filetype` when linking `libkoreader-cre.so`** — `gen_linker_exports.sh` detected macOS `ld64` instead of LLD. `base/Makefile.defs` sets `LD:=ld.lld` for `TARGET=android` to avoid this.
- **`_Float16 is not supported`** — the host compiler was the NDK's clang, not Xcode's. `base/Makefile.defs` appends (rather than prepends) the NDK bin to PATH on Darwin so bare `clang` resolves to system clang.

### Kobo

- **`cp: preserving permissions … Operation not permitted`** inside the container — UID mismatch between host and container. The script uses `--user 0:0` to sidestep this.
- **`fatal: detected dubious ownership in repository`** — same UID mismatch. The script runs `git config --global --add safe.directory '*'` inside the container.
- **`fatal: reference is not a tree … shallow.lock: File exists`** on fbink — concurrent ninja jobs racing in kodev's `clone_git_repo`. The script pre-populates fbink's source dir (with submodules) so the racy clone becomes a no-op.
- **`Could not find HOST_TRIETOOL`** — the host `libdatrie-bin` package isn't installed. The script installs it inside the container.

---

## Why macOS builds need these patches

The KOReader team's CI builds on Linux; macOS is exercised only through a dedicated macOS job. Cross-compiling to Android or Kobo from macOS hits several small Linux-isms the upstream build hasn't had reason to paper over. The changes in this repo that make it work:

- `base/Makefile.defs` — PATH-append (not prepend) for Android NDK on Darwin; `LD:=ld.lld` for Android; fix `ar` entry in the meson host toolchain template (was `$(HOSTSTRIP)`, should be `$(HOSTAR)`).
- `base/thirdparty/libdatrie/CMakeLists.txt` — replace the cross-compiled `trietool` in the staging dir with a symlink to a host one for cross builds.
- `scripts/build-android.sh` and `scripts/build-kobo-in-docker.sh` — wrap the ceremony.
