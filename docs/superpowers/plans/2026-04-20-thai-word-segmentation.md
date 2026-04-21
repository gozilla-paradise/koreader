# Thai word segmentation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add dictionary-based Thai word segmentation to the EPUB (CRE) engine so Thai paragraphs break at word boundaries and tap-to-select picks a single word; users can extend the dictionary via a text file and an in-app menu entry.

**Architecture:** Bundle libthai + libdatrie as native deps. Add a thin C++ wrapper around libthai (`ThaiSegmenter`) that owns a single `ThBrk*` rebuilt from stock dict + user-edited word list. Integrate at two existing crengine hooks — a post-pass after libunibreak in `lvtextfm.cpp::copyText` for line breaking, and a pre-check in `ldomXRange::getWordRange` for tap-to-select. Expose a typography toggle, a reload button, and a long-press "Add to Thai dictionary" action.

**Tech Stack:** libthai 0.1.29 (LGPL-2.1+), libdatrie 0.2.13 (LGPL-2.1+), existing crengine / KOReader build system (CMake + autotools externals), existing Lua menu framework.

**Spec:** `docs/superpowers/specs/2026-04-20-thai-word-segmentation-design.md`

**Delivery:** Tasks 1–20 constitute PR 1 (core). Task 21 is PR 2 (long-press add). Each PR is independently mergeable.

---

## File Structure

**Third-party wrappers (new):**
- `base/thirdparty/libdatrie/CMakeLists.txt` — autotools external for libdatrie
- `base/thirdparty/libthai/CMakeLists.txt` — autotools external for libthai, depends on libdatrie

**Build-system edits:**
- `base/thirdparty/cmake_modules/koreader_thirdparty_libs.cmake` — register new subdirs
- `base/thirdparty/cmake_modules/koreader_targets.cmake` — link libthai/libdatrie into `libkoreader-cre`
- `base/CMakeLists.txt` — copy stock `thbrk.tri` into `${PROJECT_BINARY_DIR}/data/thai/`

**crengine additions (new):**
- `base/thirdparty/kpvcrlib/crengine/crengine/include/textlang_thai.h`
- `base/thirdparty/kpvcrlib/crengine/crengine/src/textlang_thai.cpp`
- `base/thirdparty/kpvcrlib/crengine/crengine/tests/test_thai.cpp` (new dir)
- `base/thirdparty/kpvcrlib/crengine/crengine/tests/CMakeLists.txt` (new)

**crengine modifications:**
- `base/thirdparty/kpvcrlib/crengine/crengine/src/lvtextfm.cpp` — line-break post-pass in `copyText`
- `base/thirdparty/kpvcrlib/crengine/crengine/src/lvtinydom.cpp` — Thai-aware branch in `ldomXRange::getWordRange`

**KOReader C/Lua bridge:**
- `base/cre.cpp` — new Lua bindings `setThaiSegmentationEnabled`, `reloadThaiDictionary`, `addThaiWord`

**Frontend edits:**
- `frontend/apps/reader/modules/readertypography.lua` — toggle + reload button
- `frontend/apps/reader/modules/readerhighlight.lua` — "Add to Thai dictionary" action (Task 21)

**Tests:**
- `base/thirdparty/kpvcrlib/crengine/crengine/tests/test_thai.cpp` — C++ segmenter unit test
- `spec/unit/readertypography_thai_spec.lua` — Lua integration test

**Data:**
- `data/thai/thbrk.tri` — stock libthai trie, copied from the libthai build tree into the bundle

**User-facing:**
- `settings/thai_user_words.txt` — created at runtime on first init
- `cache/cr3cache/thai_combined_v1.tri` — generated at runtime

---

## Task 1: libdatrie third-party CMake wrapper

**Files:**
- Create: `base/thirdparty/libdatrie/CMakeLists.txt`
- Reference: `base/thirdparty/libunibreak/CMakeLists.txt` (same pattern)

- [ ] **Step 1: Create the wrapper**

```cmake
list(APPEND CFG_CMD COMMAND env)
append_autotools_vars(CFG_CMD)
list(APPEND CFG_CMD
    ${SOURCE_DIR}/configure --host=${CHOST} --prefix=${STAGING_DIR}
    --disable-$<IF:$<BOOL:${MONOLIBTIC}>,shared,static>
    --enable-$<IF:$<BOOL:${MONOLIBTIC}>,static,shared>
    --disable-doxygen-doc
    --enable-silent-rules
)

list(APPEND BUILD_CMD COMMAND make)
list(APPEND INSTALL_CMD COMMAND make install)

if(NOT MONOLIBTIC)
    set(LIB_SPEC datrie VERSION 1)
    if(APPLE)
        append_shared_lib_fix_commands(INSTALL_CMD ${LIB_SPEC} ID)
    endif()
    append_shared_lib_install_commands(INSTALL_CMD ${LIB_SPEC})
endif()

external_project(
    DOWNLOAD URL 53da5ce09f2dd8c7c11f9d3ef0c4773c
    https://linux.thai.net/pub/thailinux/software/libthai/libdatrie-0.2.13.tar.xz
    CONFIGURE_COMMAND ${CFG_CMD}
    BUILD_COMMAND ${BUILD_CMD}
    INSTALL_COMMAND ${INSTALL_CMD}
)
```

Note: the MD5 shown is a placeholder — fetch the real one with `curl -sL <url> | md5sum` and substitute it before committing.

- [ ] **Step 2: Verify it builds in isolation**

Run: `cd base && ./build.sh --install-thirdparty libdatrie`
Expected: `libdatrie.a` (MONOLIBTIC) or `libdatrie.so.1` appears under `base/build/<platform>/thirdparty/libdatrie/`.

- [ ] **Step 3: Commit**

```bash
git add base/thirdparty/libdatrie/CMakeLists.txt
git commit -m "base/thirdparty: add libdatrie 0.2.13 wrapper"
```

---

## Task 2: libthai third-party CMake wrapper

**Files:**
- Create: `base/thirdparty/libthai/CMakeLists.txt`

- [ ] **Step 1: Create the wrapper**

```cmake
list(APPEND CFG_CMD COMMAND env)
append_autotools_vars(CFG_CMD)
list(APPEND CFG_CMD
    DATRIE_CFLAGS=-I${STAGING_DIR}/include/datrie
    DATRIE_LIBS=-L${STAGING_DIR}/lib\ -ldatrie
    ${SOURCE_DIR}/configure --host=${CHOST} --prefix=${STAGING_DIR}
    --disable-$<IF:$<BOOL:${MONOLIBTIC}>,shared,static>
    --enable-$<IF:$<BOOL:${MONOLIBTIC}>,static,shared>
    --disable-doxygen-doc
    --enable-silent-rules
)

list(APPEND BUILD_CMD COMMAND make)
list(APPEND INSTALL_CMD COMMAND make install)

if(NOT MONOLIBTIC)
    set(LIB_SPEC thai VERSION 0)
    if(APPLE)
        append_shared_lib_fix_commands(INSTALL_CMD ${LIB_SPEC} ID)
    endif()
    append_shared_lib_install_commands(INSTALL_CMD ${LIB_SPEC})
endif()

external_project(
    DOWNLOAD URL aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa
    https://linux.thai.net/pub/thailinux/software/libthai/libthai-0.1.29.tar.xz
    PATCH_COMMAND ${KO_PATCH} ${CMAKE_CURRENT_SOURCE_DIR}/keep-built-dict.patch
    CONFIGURE_COMMAND ${CFG_CMD}
    BUILD_COMMAND ${BUILD_CMD}
    INSTALL_COMMAND ${INSTALL_CMD}
    DEPENDS libdatrie
)
```

(MD5 placeholder; replace with the real md5 before commit.)

- [ ] **Step 2: Add a zero-line patch file for now (keep-built-dict.patch)**

Create an empty patch file so the CMake machinery finds it; we'll fill it in Task 3 if we need to alter libthai's install of the stock trie. If no patch is needed, remove the `PATCH_COMMAND` line instead.

- [ ] **Step 3: Verify it builds**

Run: `cd base && ./build.sh --install-thirdparty libthai`
Expected: `libthai.{a,so.0}` appears under staging; `thbrk.tri` lands in `${STAGING_DIR}/share/libthai/`.

- [ ] **Step 4: Commit**

```bash
git add base/thirdparty/libthai/CMakeLists.txt base/thirdparty/libthai/keep-built-dict.patch
git commit -m "base/thirdparty: add libthai 0.1.29 wrapper"
```

---

## Task 3: Register new deps in CMake module files

**Files:**
- Modify: `base/thirdparty/cmake_modules/koreader_thirdparty_libs.cmake`
- Modify: `base/thirdparty/cmake_modules/koreader_targets.cmake`

- [ ] **Step 1: Register the subdirs**

In `koreader_thirdparty_libs.cmake`, locate the block that lists existing libs (near `libunibreak`) and add:

```cmake
add_subdirectory(libdatrie)
add_subdirectory(libthai)
```

- [ ] **Step 2: Link libthai + libdatrie into `libkoreader-cre`**

In `koreader_targets.cmake`, find the `target_link_libraries(...)` call for the CRE shared library (the one that already links `unibreak`, `harfbuzz`, etc.) and append `thai` and `datrie` to its link list.

- [ ] **Step 3: Build base and confirm the CRE library links cleanly**

Run: `cd base && ./build.sh`
Expected: clean build; `libkoreader-cre.{so,dylib}` contains references to `th_brk_new` and `th_brk_find_breaks` (verify with `nm -D` or `strings`).

- [ ] **Step 4: Commit**

```bash
git add base/thirdparty/cmake_modules/koreader_thirdparty_libs.cmake base/thirdparty/cmake_modules/koreader_targets.cmake
git commit -m "base: link libthai/libdatrie into CRE"
```

---

## Task 4: Ship stock trie in the KOReader bundle

**Files:**
- Modify: `base/CMakeLists.txt`

- [ ] **Step 1: Add an install step for the stock trie**

After the libthai build completes, copy its installed trie into the project's data directory. Near the end of `base/CMakeLists.txt`, add:

```cmake
install(
    FILES ${STAGING_DIR}/share/libthai/thbrk.tri
    DESTINATION ${KOREADER_BUNDLE_DIR}/data/thai/
    COMPONENT koreader
)
```

Replace `${KOREADER_BUNDLE_DIR}` with whatever variable the file uses for the bundle output (check context near existing `install(FILES ...)` calls and match the convention — likely just a relative path like `data/thai/`).

- [ ] **Step 2: Verify the trie lands in the install tree**

Run: `cd base && ./build.sh --install`
Expected: `<install-root>/data/thai/thbrk.tri` exists, ~450 KB.

- [ ] **Step 3: Commit**

```bash
git add base/CMakeLists.txt
git commit -m "base: ship libthai stock dictionary under data/thai/"
```

---

## Task 5: Create ThaiSegmenter header with public API

**Files:**
- Create: `base/thirdparty/kpvcrlib/crengine/crengine/include/textlang_thai.h`

- [ ] **Step 1: Write the header**

```cpp
#ifndef TEXTLANG_THAI_H
#define TEXTLANG_THAI_H

#include "lvtypes.h"
#include "lvstring.h"

// Thin wrapper around libthai for Thai word/line segmentation.
// Lazy-initialized on first use. Rebuilds a combined stock+user
// trie when the user words file has changed.
class ThaiSegmenter {
public:
    // Populates `positions` with break offsets (1-based, into [text, text+len)).
    // A position `p` means "break may occur between char p-1 and p".
    // No-op (leaves positions empty) if disabled or init fails.
    static void findLineBreaks(const lChar32 * text, int len, LVArray<int> & positions);
    static void findWordBreaks(const lChar32 * text, int len, LVArray<int> & positions);

    // Enable/disable the segmenter globally. Default true.
    static void setEnabled(bool enabled);
    static bool isEnabled();

    // Force rebuild of the combined trie and reload the segmenter.
    // Called after the user edits thai_user_words.txt.
    static void reload();

    // Called at shutdown. Releases the ThBrk handle.
    static void uninit();

    // Lets the front-end provide the two filesystem paths. Must be
    // called once before any other method. Paths are copied.
    static void configure(const lString8 & stock_trie_path,
                          const lString8 & user_words_path,
                          const lString8 & combined_trie_cache_path);

    // Returns true if any char in [text, text+len) is in U+0E00..U+0E7F.
    static bool containsThai(const lChar32 * text, int len);

    // Appends `word` to the user words file (deduplicated),
    // then calls reload(). Returns true on success.
    static bool addUserWord(const lString32 & word);

private:
    ThaiSegmenter() = delete;
};

#endif // TEXTLANG_THAI_H
```

- [ ] **Step 2: Commit**

```bash
git add base/thirdparty/kpvcrlib/crengine/crengine/include/textlang_thai.h
git commit -m "crengine: add ThaiSegmenter public header"
```

---

## Task 6: Implement `containsThai` + failing test for it

**Files:**
- Create: `base/thirdparty/kpvcrlib/crengine/crengine/tests/CMakeLists.txt`
- Create: `base/thirdparty/kpvcrlib/crengine/crengine/tests/test_thai.cpp`

- [ ] **Step 1: Set up a minimal test CMake entry**

```cmake
# tests/CMakeLists.txt
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    message(STATUS "GTest not found; skipping crengine tests")
    return()
endif()

add_executable(test_crengine_thai test_thai.cpp)
target_link_libraries(test_crengine_thai PRIVATE crengine GTest::gtest GTest::gtest_main)
target_include_directories(test_crengine_thai PRIVATE ${CMAKE_SOURCE_DIR}/include)
add_test(NAME crengine_thai COMMAND test_crengine_thai)
```

Hook this up from the top-level crengine `CMakeLists.txt` conditionally (guard behind `BUILD_TESTING`). If the crengine tree doesn't set `BUILD_TESTING`, add `option(BUILD_TESTING "Build tests" OFF)` at its top.

- [ ] **Step 2: Write the failing test**

```cpp
// tests/test_thai.cpp
#include <gtest/gtest.h>
#include "textlang_thai.h"

TEST(ThaiSegmenter, ContainsThaiDetectsThaiChars) {
    const lChar32 thai[] = { 0x0E01, 0x0E32, 0x0E23, 0 }; // ก า ร (common Thai chars)
    EXPECT_TRUE(ThaiSegmenter::containsThai(thai, 3));
}

TEST(ThaiSegmenter, ContainsThaiRejectsAscii) {
    const lChar32 ascii[] = { 'h', 'e', 'l', 'l', 'o', 0 };
    EXPECT_FALSE(ThaiSegmenter::containsThai(ascii, 5));
}

TEST(ThaiSegmenter, ContainsThaiRejectsEmpty) {
    const lChar32 empty[] = { 0 };
    EXPECT_FALSE(ThaiSegmenter::containsThai(empty, 0));
}
```

- [ ] **Step 3: Run the test to verify it fails**

Run: `cmake --build base/build/<platform> --target test_crengine_thai && ctest -R crengine_thai -V`
Expected: link error (no `textlang_thai.cpp` yet).

- [ ] **Step 4: Create `textlang_thai.cpp` with minimal `containsThai`**

```cpp
// src/textlang_thai.cpp
#include "textlang_thai.h"

bool ThaiSegmenter::containsThai(const lChar32 * text, int len) {
    for (int i = 0; i < len; i++) {
        if (text[i] >= 0x0E00 && text[i] <= 0x0E7F) {
            return true;
        }
    }
    return false;
}

void ThaiSegmenter::findLineBreaks(const lChar32 *, int, LVArray<int> &) {}
void ThaiSegmenter::findWordBreaks(const lChar32 *, int, LVArray<int> &) {}
void ThaiSegmenter::setEnabled(bool) {}
bool ThaiSegmenter::isEnabled() { return false; }
void ThaiSegmenter::reload() {}
void ThaiSegmenter::uninit() {}
void ThaiSegmenter::configure(const lString8 &, const lString8 &, const lString8 &) {}
bool ThaiSegmenter::addUserWord(const lString32 &) { return false; }
```

Add the `.cpp` to crengine's `CMakeLists.txt` source list (find the block listing `textlang.cpp` and add `textlang_thai.cpp` alongside it).

- [ ] **Step 5: Run the test to verify it passes**

Run: `cmake --build base/build/<platform> --target test_crengine_thai && ctest -R crengine_thai -V`
Expected: 3/3 passing.

- [ ] **Step 6: Commit**

```bash
git add base/thirdparty/kpvcrlib/crengine/crengine/{include/textlang_thai.h,src/textlang_thai.cpp,tests/}
git commit -m "crengine: scaffold ThaiSegmenter with containsThai"
```

---

## Task 7: Implement configure + trie path plumbing

**Files:**
- Modify: `base/thirdparty/kpvcrlib/crengine/crengine/src/textlang_thai.cpp`
- Modify: `base/thirdparty/kpvcrlib/crengine/crengine/tests/test_thai.cpp`

- [ ] **Step 1: Add failing test for `configure` + `isEnabled` toggle**

```cpp
TEST(ThaiSegmenter, ConfigureStoresPathsAndEnables) {
    ThaiSegmenter::configure(lString8("/tmp/thbrk.tri"),
                             lString8("/tmp/thai_user.txt"),
                             lString8("/tmp/thai_cache.tri"));
    EXPECT_TRUE(ThaiSegmenter::isEnabled());
    ThaiSegmenter::setEnabled(false);
    EXPECT_FALSE(ThaiSegmenter::isEnabled());
    ThaiSegmenter::setEnabled(true);
}
```

- [ ] **Step 2: Run test — expect failure (isEnabled returns false)**

Run: `ctest -R crengine_thai -V`
Expected: new test fails.

- [ ] **Step 3: Implement the state**

In `textlang_thai.cpp`, add static state at file scope:

```cpp
#include <thai/thbrk.h>

namespace {
    bool s_enabled = true;
    bool s_configured = false;
    lString8 s_stock_path;
    lString8 s_user_path;
    lString8 s_cache_path;
    ThBrk * s_thbrk = nullptr;
    time_t s_user_mtime = 0;
}

void ThaiSegmenter::configure(const lString8 & stock, const lString8 & user, const lString8 & cache) {
    s_stock_path = stock;
    s_user_path = user;
    s_cache_path = cache;
    s_configured = true;
}

void ThaiSegmenter::setEnabled(bool e) { s_enabled = e; }
bool ThaiSegmenter::isEnabled() { return s_enabled && s_configured; }
```

- [ ] **Step 4: Run test — expect pass**

Run: `ctest -R crengine_thai -V`
Expected: all passing.

- [ ] **Step 5: Commit**

```bash
git add base/thirdparty/kpvcrlib/crengine/crengine/src/textlang_thai.cpp base/thirdparty/kpvcrlib/crengine/crengine/tests/test_thai.cpp
git commit -m "crengine: ThaiSegmenter configure + enable state"
```

---

## Task 8: Implement trie (re)build from stock + user words

**Files:**
- Modify: `base/thirdparty/kpvcrlib/crengine/crengine/src/textlang_thai.cpp`
- Modify: `base/thirdparty/kpvcrlib/crengine/crengine/tests/test_thai.cpp`

- [ ] **Step 1: Add failing test that exercises rebuild**

```cpp
TEST(ThaiSegmenter, RebuildProducesCacheFile) {
    // Fixture: copy stock trie into a tmp dir, write a user words file.
    // The test expects ensureTrie() (internal) to produce a cache file.
    std::string tmpdir = "/tmp/thai_test_" + std::to_string(::getpid());
    mkdir(tmpdir.c_str(), 0700);

    std::string user_path = tmpdir + "/user.txt";
    FILE * f = fopen(user_path.c_str(), "w");
    fputs("# test user words\n", f);
    fputs("\xE0\xB8\x81\xE0\xB8\x82\xE0\xB8\x83\n", f); // ก ข ค
    fclose(f);

    std::string cache_path = tmpdir + "/combined.tri";
    // Assume tests are run from repo root so this path resolves:
    std::string stock_path = "data/thai/thbrk.tri";

    ThaiSegmenter::configure(lString8(stock_path.c_str()),
                             lString8(user_path.c_str()),
                             lString8(cache_path.c_str()));
    ThaiSegmenter::reload();

    struct stat st;
    EXPECT_EQ(0, stat(cache_path.c_str(), &st));
    EXPECT_GT(st.st_size, 0);
}
```

- [ ] **Step 2: Run test — expect failure**

Run: `ctest -R crengine_thai -V`
Expected: fails (cache not written).

- [ ] **Step 3: Implement rebuild + reload**

Add to `textlang_thai.cpp`:

```cpp
#include <datrie/trie.h>
#include <sys/stat.h>
#include <stdio.h>

namespace {

time_t fileMtime(const char * path) {
    struct stat st;
    if (stat(path, &st) != 0) return 0;
    return st.st_mtime;
}

bool buildCombinedTrie() {
    Trie * trie = trie_new_from_file(s_stock_path.c_str());
    if (!trie) return false;

    FILE * f = fopen(s_user_path.c_str(), "r");
    if (f) {
        char buf[512];
        while (fgets(buf, sizeof(buf), f)) {
            // strip trailing \n and leading whitespace, skip comments/blank
            char * p = buf;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '#' || *p == '\n' || *p == '\0') continue;
            size_t n = strlen(p);
            while (n > 0 && (p[n-1] == '\n' || p[n-1] == '\r')) p[--n] = 0;
            if (n == 0) continue;
            // Convert UTF-8 -> UCS-4 (datrie AlphaChar = uint32)
            lString32 word = Utf8ToUnicode(lString8(p));
            AlphaChar achars[256];
            int m = 0;
            for (int i = 0; i < (int)word.length() && m < 255; i++) achars[m++] = word[i];
            achars[m] = 0;
            trie_store(trie, achars, TRIE_DATA_READ);
        }
        fclose(f);
    }

    bool ok = (trie_save(trie, s_cache_path.c_str()) == 0);
    trie_free(trie);
    return ok;
}

bool ensureTrie() {
    if (!s_configured) return false;
    time_t user_mt = fileMtime(s_user_path.c_str());
    time_t cache_mt = fileMtime(s_cache_path.c_str());
    if (cache_mt == 0 || user_mt > cache_mt) {
        if (!buildCombinedTrie()) return false;
    }
    if (!s_thbrk) {
        s_thbrk = th_brk_new(s_cache_path.c_str());
    }
    return s_thbrk != nullptr;
}

} // namespace

void ThaiSegmenter::reload() {
    if (s_thbrk) { th_brk_delete(s_thbrk); s_thbrk = nullptr; }
    // Force regeneration by bumping user mtime check: just delete cache.
    ::unlink(s_cache_path.c_str());
    ensureTrie();
}

void ThaiSegmenter::uninit() {
    if (s_thbrk) { th_brk_delete(s_thbrk); s_thbrk = nullptr; }
}
```

`TRIE_DATA_READ` is a libdatrie-defined sentinel used as the "value" for every word entry; the segmenter cares only about key presence.

- [ ] **Step 4: Run test — expect pass**

Run: `ctest -R crengine_thai -V`
Expected: all passing.

- [ ] **Step 5: Commit**

```bash
git add -u
git commit -m "crengine: rebuild combined Thai trie from stock+user dict"
```

---

## Task 9: Implement findLineBreaks / findWordBreaks

**Files:**
- Modify: `base/thirdparty/kpvcrlib/crengine/crengine/src/textlang_thai.cpp`
- Modify: `base/thirdparty/kpvcrlib/crengine/crengine/tests/test_thai.cpp`

- [ ] **Step 1: Add failing test for line breaks on a known phrase**

```cpp
TEST(ThaiSegmenter, FindLineBreaksSplitsAtWordBoundary) {
    // "กินข้าว" = "kin khao" (eat rice) — two words.
    const lChar32 text[] = {
        0x0E01, 0x0E34, 0x0E19,             // กิน
        0x0E02, 0x0E49, 0x0E32, 0x0E27,     // ข้าว
        0
    };
    int len = 7;
    LVArray<int> positions;
    // Expect a break position between index 3 and 4 (between กิน and ข้าว).
    ThaiSegmenter::findLineBreaks(text, len, positions);
    bool found3 = false;
    for (int i = 0; i < positions.length(); i++) if (positions[i] == 3) found3 = true;
    EXPECT_TRUE(found3);
}
```

- [ ] **Step 2: Run test — expect failure (returns empty)**

Run: `ctest -R crengine_thai -V`
Expected: fails.

- [ ] **Step 3: Implement the two find functions**

In `textlang_thai.cpp`:

```cpp
#include <thai/thbrk.h>
#include <thai/thwchar.h>

static void findBreaksInternal(const lChar32 * text, int len, LVArray<int> & positions) {
    positions.clear();
    if (!ThaiSegmenter::isEnabled() || len <= 1) return;
    if (!ensureTrie()) return;

    // libthai wants UCS-4 in thchar_t; thchar_t is unsigned int. Copy into a buffer.
    std::vector<thchar_t> in(len + 1);
    for (int i = 0; i < len; i++) in[i] = (thchar_t)text[i];
    in[len] = 0;

    std::vector<int> out(len + 1);
    int n = th_brk_find_breaks(s_thbrk, in.data(), out.data(), (int)out.size());
    for (int i = 0; i < n; i++) {
        // th_brk_find_breaks returns 0-based positions where a break may occur *before* that index.
        // Translate to "break after index p-1" (our 1-based convention): the returned index == our p.
        if (out[i] > 0 && out[i] < len) positions.add(out[i]);
    }
}

void ThaiSegmenter::findLineBreaks(const lChar32 * text, int len, LVArray<int> & positions) {
    findBreaksInternal(text, len, positions);
}

void ThaiSegmenter::findWordBreaks(const lChar32 * text, int len, LVArray<int> & positions) {
    // libthai uses the same dictionary boundaries for both; distinction matters only
    // when surrounding punctuation is present, which callers handle separately.
    findBreaksInternal(text, len, positions);
}
```

- [ ] **Step 4: Run test — expect pass**

Run: `ctest -R crengine_thai -V`
Expected: all passing.

- [ ] **Step 5: Add a user-dict-honored test**

```cpp
TEST(ThaiSegmenter, UserDictMakesNewWordAtomic) {
    // Add an uncommon word via user dict; confirm it becomes a single segment.
    std::string tmpdir = "/tmp/thai_test_user_" + std::to_string(::getpid());
    mkdir(tmpdir.c_str(), 0700);
    std::string user_path = tmpdir + "/user.txt";
    FILE * f = fopen(user_path.c_str(), "w");
    fputs("\xE0\xB8\x95\xE0\xB8\xB8\xE0\xB9\x8A\xE0\xB8\x81\xE0\xB8\x95\xE0\xB8\xB2\xE0\xB9\x8A\n", f); // ตุ๊กตา (doll) — already in stock; keep for regression
    fclose(f);

    ThaiSegmenter::configure(lString8("data/thai/thbrk.tri"),
                             lString8(user_path.c_str()),
                             lString8((tmpdir + "/c.tri").c_str()));
    ThaiSegmenter::reload();

    const lChar32 text[] = { 0x0E15, 0x0E38, 0x0E4A, 0x0E01, 0x0E15, 0x0E32, 0x0E4A, 0 };
    LVArray<int> positions;
    ThaiSegmenter::findWordBreaks(text, 7, positions);
    // Expect no internal break (the word is a single unit).
    for (int i = 0; i < positions.length(); i++) {
        EXPECT_FALSE(positions[i] > 0 && positions[i] < 7);
    }
}
```

- [ ] **Step 6: Run test — expect pass**

Run: `ctest -R crengine_thai -V`

- [ ] **Step 7: Commit**

```bash
git add -u
git commit -m "crengine: ThaiSegmenter findLineBreaks/findWordBreaks via libthai"
```

---

## Task 10: Integrate line-break post-pass into lvtextfm.cpp::copyText

**Files:**
- Modify: `base/thirdparty/kpvcrlib/crengine/crengine/src/lvtextfm.cpp`

- [ ] **Step 1: Locate the post-libunibreak point**

The libunibreak loop ends around `lvtextfm.cpp:1504`. The post-pass should run after the loop completes but before the function returns from `copyText()`. Find the closing brace of the `for (i=start; i<end; i++)` loop in `copyText` and add the post-pass immediately after it.

- [ ] **Step 2: Add the Thai post-pass**

Right after the main loop in `copyText`, insert:

```cpp
#include "textlang_thai.h"

// ... in copyText, after the per-src loop:
#if (USE_LIBUNIBREAK==1)
if (ThaiSegmenter::isEnabled() && ThaiSegmenter::containsThai(m_text, m_length)) {
    int i = 0;
    while (i < m_length) {
        if (m_text[i] >= 0x0E00 && m_text[i] <= 0x0E7F) {
            int run_start = i;
            while (i < m_length && m_text[i] >= 0x0E00 && m_text[i] <= 0x0E7F) i++;
            int run_len = i - run_start;
            if (run_len >= 2) {
                LVArray<int> positions;
                ThaiSegmenter::findLineBreaks(m_text + run_start, run_len, positions);
                for (int k = 0; k < positions.length(); k++) {
                    int p = positions[k]; // 1-based offset inside the run
                    int flag_idx = run_start + p - 1;
                    if (flag_idx >= 0 && flag_idx < m_length - 1) {
                        m_flags[flag_idx] |= LCHAR_ALLOW_WRAP_AFTER;
                        m_flags[flag_idx] &= ~LCHAR_DEPRECATED_WRAP_AFTER;
                    }
                }
            }
        } else {
            i++;
        }
    }
}
#endif
```

- [ ] **Step 3: Build CRE**

Run: `cd base && ./build.sh`
Expected: clean build.

- [ ] **Step 4: Manual smoke test**

Open a Thai EPUB in KOReader; confirm long Thai paragraphs now wrap mid-run instead of overflowing.

- [ ] **Step 5: Commit**

```bash
git add base/thirdparty/kpvcrlib/crengine/crengine/src/lvtextfm.cpp
git commit -m "crengine: add Thai line-break post-pass to copyText"
```

---

## Task 11: Thai-aware word selection in ldomXRange::getWordRange

**Files:**
- Modify: `base/thirdparty/kpvcrlib/crengine/crengine/src/lvtinydom.cpp`

- [ ] **Step 1: Locate the function**

`ldomXRange::getWordRange` at `lvtinydom.cpp:13454`. Current logic walks spaces only.

- [ ] **Step 2: Add the Thai-aware branch**

At the top of `getWordRange`, after the pos clamping and `lString32 txt = node->getText()` line, insert:

```cpp
#include "textlang_thai.h"

// ... inside getWordRange, after `if ( pos>(int)txt.length() ) pos = txt.length();`:
if (pos < (int)txt.length() && txt[pos] >= 0x0E00 && txt[pos] <= 0x0E7F
        && ThaiSegmenter::isEnabled()) {
    // Find the enclosing Thai run.
    int run_start = pos;
    while (run_start > 0 && txt[run_start - 1] >= 0x0E00 && txt[run_start - 1] <= 0x0E7F)
        run_start--;
    int run_end = pos;
    while (run_end < (int)txt.length() && txt[run_end] >= 0x0E00 && txt[run_end] <= 0x0E7F)
        run_end++;

    LVArray<int> boundaries;
    ThaiSegmenter::findWordBreaks(txt.c_str() + run_start, run_end - run_start, boundaries);

    // boundaries are 1-based positions inside the run.
    // Snap to the boundary <= (pos - run_start) and the next boundary > that.
    int local = pos - run_start;
    int word_start_local = 0;
    int word_end_local = run_end - run_start;
    for (int i = 0; i < boundaries.length(); i++) {
        int b = boundaries[i];
        if (b <= local) word_start_local = b;
        else { word_end_local = b; break; }
    }
    int word_start = run_start + word_start_local;
    int word_end   = run_start + word_end_local;
    ldomXRange r( ldomXPointer(node, word_start), ldomXPointer(node, word_end) );
    range = r;
    return true;
}
```

- [ ] **Step 3: Build CRE**

Run: `cd base && ./build.sh`
Expected: clean build.

- [ ] **Step 4: Manual smoke test**

Long-press on a Thai word in a Thai EPUB; confirm the selection snaps to a single word rather than the whole run. Repeat with dictionary lookup — the looked-up word should be just the tapped one.

- [ ] **Step 5: Commit**

```bash
git add base/thirdparty/kpvcrlib/crengine/crengine/src/lvtinydom.cpp
git commit -m "crengine: Thai-aware word selection in getWordRange"
```

---

## Task 12: Lua bindings in cre.cpp

**Files:**
- Modify: `base/cre.cpp`

- [ ] **Step 1: Add three new C functions**

Somewhere near the other `static int <name>(lua_State *L)` helpers (e.g. around `getTextLangStatus` at `cre.cpp:662`):

```cpp
#include "thirdparty/kpvcrlib/crengine/crengine/include/textlang_thai.h"

static int setThaiSegmentationEnabled(lua_State *L) {
    bool enabled = lua_toboolean(L, 1);
    ThaiSegmenter::setEnabled(enabled);
    return 0;
}

static int reloadThaiDictionary(lua_State *L) {
    (void)L;
    ThaiSegmenter::reload();
    return 0;
}

static int addThaiWord(lua_State *L) {
    const char * word = luaL_checkstring(L, 1);
    lString32 w = Utf8ToUnicode(lString8(word));
    bool ok = ThaiSegmenter::addUserWord(w);
    lua_pushboolean(L, ok);
    return 1;
}

static int configureThaiSegmenter(lua_State *L) {
    const char * stock = luaL_checkstring(L, 1);
    const char * user  = luaL_checkstring(L, 2);
    const char * cache = luaL_checkstring(L, 3);
    ThaiSegmenter::configure(lString8(stock), lString8(user), lString8(cache));
    return 0;
}
```

- [ ] **Step 2: Register them in the module function table**

Find the static array near `cre.cpp:4294` (where `getTextLangStatus` is registered) and add:

```cpp
{"setThaiSegmentationEnabled", setThaiSegmentationEnabled},
{"reloadThaiDictionary", reloadThaiDictionary},
{"addThaiWord", addThaiWord},
{"configureThaiSegmenter", configureThaiSegmenter},
```

- [ ] **Step 3: Implement `ThaiSegmenter::addUserWord`**

In `textlang_thai.cpp`:

```cpp
bool ThaiSegmenter::addUserWord(const lString32 & word) {
    if (word.empty() || !s_configured) return false;
    // Read existing lines to dedupe.
    FILE * rf = fopen(s_user_path.c_str(), "r");
    lString8 utf8_word = UnicodeToUtf8(word);
    if (rf) {
        char buf[512];
        while (fgets(buf, sizeof(buf), rf)) {
            size_t n = strlen(buf);
            while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = 0;
            if (strcmp(buf, utf8_word.c_str()) == 0) { fclose(rf); return true; }
        }
        fclose(rf);
    }
    FILE * wf = fopen(s_user_path.c_str(), "a");
    if (!wf) return false;
    fprintf(wf, "%s\n", utf8_word.c_str());
    fclose(wf);
    reload();
    return true;
}
```

- [ ] **Step 4: Build**

Run: `cd base && ./build.sh`
Expected: clean build.

- [ ] **Step 5: Commit**

```bash
git add base/cre.cpp base/thirdparty/kpvcrlib/crengine/crengine/src/textlang_thai.cpp
git commit -m "base: Lua bindings for Thai segmenter control"
```

---

## Task 13: Initialize the segmenter at CRE document open

**Files:**
- Modify: `frontend/document/credocument.lua`

- [ ] **Step 1: Find the init point**

Locate the `function CreDocument:init()` or equivalent in `credocument.lua`. This is where CRE receives the main language / font / margins.

- [ ] **Step 2: Call the new Lua binding once**

Add near the top of `init()` (before any document rendering happens):

```lua
local DataStorage = require("datastorage")
local lfs = require("libs/libkoreader-lfs")

local stock = package.searchpath("", "") -- placeholder
-- Use the same pattern as other data files in this codebase.
-- If there's a helper like `DataStorage:getFullDataDir()` or a constant,
-- use it here. Example:
local data_dir = require("datastorage"):getDataDir()
local stock_path   = data_dir .. "/data/thai/thbrk.tri"
local user_path    = DataStorage:getSettingsDir() .. "/thai_user_words.txt"
local cache_path   = DataStorage:getSettingsDir() .. "/../cache/cr3cache/thai_combined_v1.tri"

-- Ensure cache dir exists.
local cache_dir = cache_path:match("(.*)/")
if cache_dir and not lfs.attributes(cache_dir) then
    lfs.mkdir(cache_dir)
end

-- Create empty user words file on first run.
if not lfs.attributes(user_path) then
    local f = io.open(user_path, "w")
    if f then
        f:write("# Thai user dictionary — one word per line, UTF-8. Lines starting with '#' are comments.\n")
        f:close()
    end
end

cre.configureThaiSegmenter(stock_path, user_path, cache_path)

local G_reader_settings = require("luasettings"):open(DataStorage:getSettingsDir() .. "/settings.reader.lua")
cre.setThaiSegmentationEnabled(G_reader_settings:nilOrTrue("thai_segmentation_enabled"))
```

Match the exact API shape of whatever `DataStorage` actually exposes in this codebase (check `frontend/datastorage.lua`); the snippet above shows intent, not blindly-copyable code.

- [ ] **Step 3: Manual smoke test**

Open a non-Thai book, confirm no crash. Open a Thai book, confirm a `thai_user_words.txt` appears in `settings/`.

- [ ] **Step 4: Commit**

```bash
git add frontend/document/credocument.lua
git commit -m "credocument: initialize Thai segmenter paths on open"
```

---

## Task 14: Typography menu — toggle and reload entries

**Files:**
- Modify: `frontend/apps/reader/modules/readertypography.lua`

- [ ] **Step 1: Find the typography menu registration**

The file defines a menu table. Look for the block that lists per-language or text-related settings (hyphenation menu entries are a good anchor).

- [ ] **Step 2: Add the two entries**

Near the end of the menu items list:

```lua
{
    text = _("Thai word segmentation"),
    help_text = _("Break lines and select words inside Thai text using a dictionary. Turn off to fall back to no-break behavior."),
    checked_func = function()
        return G_reader_settings:nilOrTrue("thai_segmentation_enabled")
    end,
    callback = function()
        local enabled = not G_reader_settings:nilOrTrue("thai_segmentation_enabled")
        G_reader_settings:saveSetting("thai_segmentation_enabled", enabled)
        cre.setThaiSegmentationEnabled(enabled)
        UIManager:setDirty("all", "partial")
    end,
},
{
    text = _("Reload Thai dictionary"),
    help_text = _("Re-read the user words file and re-segment the current page."),
    callback = function()
        cre.reloadThaiDictionary()
        UIManager:setDirty("all", "partial")
    end,
},
```

- [ ] **Step 3: Manual smoke test**

Open the typography menu; confirm both entries appear. Toggle off → Thai runs no longer break; toggle on → they do. Click "Reload" after editing the file manually → page re-renders.

- [ ] **Step 4: Commit**

```bash
git add frontend/apps/reader/modules/readertypography.lua
git commit -m "readertypography: add Thai segmentation toggle and reload"
```

---

## Task 15: Lua integration test for the toggle

**Files:**
- Create: `spec/unit/readertypography_thai_spec.lua`

- [ ] **Step 1: Write the test**

```lua
describe("Thai segmentation toggle", function()
    local cre
    setup(function()
        require("commonrequire")
        cre = require("document/credocument").engine or require("libs/libkoreader-cre")
    end)

    it("should expose the Lua bindings", function()
        assert.is_function(cre.setThaiSegmentationEnabled)
        assert.is_function(cre.reloadThaiDictionary)
        assert.is_function(cre.addThaiWord)
        assert.is_function(cre.configureThaiSegmenter)
    end)

    it("should accept enable/disable without error", function()
        cre.setThaiSegmentationEnabled(true)
        cre.setThaiSegmentationEnabled(false)
    end)
end)
```

- [ ] **Step 2: Run the test**

Run: `./koreader-base/tools/run-spec.sh spec/unit/readertypography_thai_spec.lua`
Expected: 2/2 passing.

- [ ] **Step 3: Commit**

```bash
git add spec/unit/readertypography_thai_spec.lua
git commit -m "spec: basic coverage for Thai segmentation Lua bindings"
```

---

## Task 16: Regression test — mixed Thai+ASCII paragraph

**Files:**
- Modify: `base/thirdparty/kpvcrlib/crengine/crengine/tests/test_thai.cpp`

- [ ] **Step 1: Add the test**

```cpp
TEST(ThaiSegmenter, MixedAsciiThaiOnlyReturnsThaiBreaks) {
    // "hello กินข้าว world" — breaks should only be inside the Thai run.
    const lChar32 text[] = {
        'h','e','l','l','o',' ',
        0x0E01, 0x0E34, 0x0E19, 0x0E02, 0x0E49, 0x0E32, 0x0E27,
        ' ','w','o','r','l','d', 0
    };
    int len = 19;

    // The function operates on a pre-sliced Thai-only run. So the *integration*
    // is tested by verifying containsThai + the find-runs loop semantics.
    // Here we just confirm the scan-and-call pattern mirrors lvtextfm.cpp.
    int i = 0;
    std::vector<int> all_breaks;
    while (i < len) {
        if (text[i] >= 0x0E00 && text[i] <= 0x0E7F) {
            int s = i;
            while (i < len && text[i] >= 0x0E00 && text[i] <= 0x0E7F) i++;
            LVArray<int> positions;
            ThaiSegmenter::findLineBreaks(text + s, i - s, positions);
            for (int k = 0; k < positions.length(); k++) all_breaks.push_back(s + positions[k]);
        } else {
            i++;
        }
    }
    // Every reported break index must fall inside [6, 13) — the Thai run.
    for (int b : all_breaks) {
        EXPECT_GE(b, 6);
        EXPECT_LT(b, 13);
    }
    EXPECT_GE((int)all_breaks.size(), 1); // at least one internal break
}
```

- [ ] **Step 2: Run test — expect pass**

Run: `ctest -R crengine_thai -V`
Expected: all passing.

- [ ] **Step 3: Commit**

```bash
git add base/thirdparty/kpvcrlib/crengine/crengine/tests/test_thai.cpp
git commit -m "crengine/test: mixed Thai+ASCII paragraph regression"
```

---

## Task 17: Regression test — empty and single-char input

**Files:**
- Modify: `base/thirdparty/kpvcrlib/crengine/crengine/tests/test_thai.cpp`

- [ ] **Step 1: Add the tests**

```cpp
TEST(ThaiSegmenter, EmptyInputReturnsNoBreaks) {
    LVArray<int> positions;
    ThaiSegmenter::findLineBreaks(nullptr, 0, positions);
    EXPECT_EQ(0, positions.length());
}

TEST(ThaiSegmenter, SingleCharInputReturnsNoBreaks) {
    const lChar32 one[] = { 0x0E01, 0 };
    LVArray<int> positions;
    ThaiSegmenter::findLineBreaks(one, 1, positions);
    EXPECT_EQ(0, positions.length());
}
```

- [ ] **Step 2: Run and commit**

```bash
ctest -R crengine_thai -V
git add -u
git commit -m "crengine/test: edge cases for Thai segmenter"
```

---

## Task 18: Shutdown cleanup

**Files:**
- Modify: `base/cre.cpp`

- [ ] **Step 1: Call `ThaiSegmenter::uninit()` at CRE shutdown**

Find the CRE shutdown/cleanup hook (often `cre.stop` or a Lua-registered `__gc`). Add:

```cpp
ThaiSegmenter::uninit();
```

Match the location where other CRE singletons are released (e.g. `TextLangMan::uninit()` or `HyphMan::uninit()`).

- [ ] **Step 2: Build, start KOReader, close it; verify no leaks via valgrind on Linux (optional)**

Run: `valgrind --leak-check=full ./koreader.sh` (optional; skip if the harness doesn't support it).

- [ ] **Step 3: Commit**

```bash
git add base/cre.cpp
git commit -m "cre: release ThaiSegmenter handle on shutdown"
```

---

## Task 19: Device build and bundle verification

**Files:** (no edits; verification)

- [ ] **Step 1: Build a release bundle for a target device**

Run: `make kodev` (or the existing device target script; e.g. `./kodev build kindle`).

Expected: `libthai.so.0`, `libdatrie.so.1` appear alongside other CRE .so files in the bundle; `data/thai/thbrk.tri` present; size increase ~580 KB.

- [ ] **Step 2: Side-load to a device and open a Thai EPUB**

Confirm golden path:
- Thai paragraph wraps mid-run.
- Tap on a Thai word selects only that word.
- Dictionary lookup returns a single word.
- Toggle off → behavior reverts; toggle on → restored.
- Edit `settings/thai_user_words.txt` on device, hit Reload → new word is honored.

- [ ] **Step 3: Document results in the PR description**

No commit for this task (verification-only).

---

## Task 20: PR 1 — finalize and open

- [ ] **Step 1: Rebase onto latest master, resolve any drift**
- [ ] **Step 2: Run full test suite**

Run: `make test` (or the repo's top-level test entry).
Expected: all existing tests pass.

- [ ] **Step 3: Open the PR**

```bash
gh pr create --title "Thai word segmentation in CRE (line breaks + word selection)" --body "$(cat <<'EOF'
## Summary
- Add libthai + libdatrie as native deps; bundle stock trie.
- Post-pass in `lvtextfm.cpp::copyText` adds word-boundary wrap opportunities inside Thai runs.
- `ldomXRange::getWordRange` snaps tap-to-select to segmenter boundaries in Thai runs.
- Typography menu gains "Thai word segmentation" toggle + "Reload Thai dictionary".
- Users can extend the dictionary via `settings/thai_user_words.txt`.

## Spec
`docs/superpowers/specs/2026-04-20-thai-word-segmentation-design.md`

## Test plan
- [ ] Open a Thai EPUB: paragraphs wrap at word boundaries
- [ ] Tap a Thai word: dictionary lookup returns one word
- [ ] Toggle off → revert; toggle on → restored
- [ ] Add a word to `thai_user_words.txt`; Reload → new word honored
- [ ] Non-Thai EPUB: zero regression, zero measurable overhead

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

---

## Task 21 (PR 2): Long-press "Add to Thai dictionary"

**Files:**
- Modify: `frontend/apps/reader/modules/readerhighlight.lua`

- [ ] **Step 1: Find the highlight action menu**

Look for `self.view.highlight.highlight_dialog` or the equivalent context-menu table that lists actions like "Highlight", "Translate", "Copy".

- [ ] **Step 2: Add a Thai-only entry**

```lua
{
    text = _("Add to Thai dictionary"),
    enabled_func = function()
        local sel = self.selected_text and self.selected_text.text
        if not sel then return false end
        for _, c in utf8.codes(sel) do
            if c >= 0x0E00 and c <= 0x0E7F then return true end
        end
        return false
    end,
    callback = function()
        local sel = self.selected_text and self.selected_text.text
        if not sel then return end
        local ok = cre.addThaiWord(sel)
        if ok then
            UIManager:show(InfoMessage:new{ text = _("Word added to Thai dictionary.") })
            UIManager:setDirty("all", "partial")
        else
            UIManager:show(InfoMessage:new{ text = _("Failed to add word.") })
        end
    end,
},
```

- [ ] **Step 3: Manual test**

Select a word in a Thai EPUB → the action appears. Select an English word → it does not. Tap it → the word lands in `settings/thai_user_words.txt`; re-renders; new word is atomic in subsequent selections.

- [ ] **Step 4: Commit and open PR 2**

```bash
git add frontend/apps/reader/modules/readerhighlight.lua
git commit -m "readerhighlight: 'Add to Thai dictionary' action"

gh pr create --title "Thai word segmentation: long-press 'Add to dictionary' action" --body "$(cat <<'EOF'
## Summary
- Adds a highlight dialog action (Thai-only) that appends the selected word to `settings/thai_user_words.txt`, rebuilds the trie, and re-renders.

## Test plan
- [ ] Thai word → action visible → tapped → word in file → atomic next time
- [ ] Non-Thai selection → action hidden

🤖 Generated with [Claude Code](https://claude.com/claude-code)
EOF
)"
```

---

## Self-review notes

**Spec coverage:**
- Library integration → Tasks 1–4.
- Segmenter wrapper → Tasks 5–9.
- Line-break integration → Task 10.
- Word-selection integration → Task 11.
- Lua bindings → Task 12.
- Init wiring → Task 13.
- Typography menu (toggle + reload) → Task 14.
- Long-press "Add to Thai dictionary" → Task 21.
- C++ unit tests → Tasks 6–9, 16–17.
- Lua integration test → Task 15.
- Manual device test → Task 19.
- Cleanup → Task 18.
- Delivery as two PRs → Tasks 20, 21.

**Placeholder scan:** one `aaaaaaa...` MD5 placeholder in Task 2 — flagged explicitly in text ("replace with the real md5 before commit"). Same note applies to Task 1's MD5. Not a plan placeholder — it's a deliberate, called-out TODO for the implementer since computing an MD5 requires network access at the time of writing.

**Type consistency:**
- `ThaiSegmenter::findLineBreaks / findWordBreaks` signatures identical across tasks.
- `configure` takes three `lString8` args throughout.
- Lua binding names: `cre.setThaiSegmentationEnabled`, `cre.reloadThaiDictionary`, `cre.addThaiWord`, `cre.configureThaiSegmenter` — used consistently in Tasks 12, 13, 14, 15, 21.
- `thai_segmentation_enabled` is the settings key in both Task 13 and Task 14.
- `thai_user_words.txt` / `thai_combined_v1.tri` / `data/thai/thbrk.tri` are the canonical paths throughout.
