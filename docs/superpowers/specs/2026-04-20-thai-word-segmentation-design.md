# Thai word segmentation in KOReader (CRE / EPUB)

**Status:** Design
**Date:** 2026-04-20
**Scope:** EPUB engine (crengine / CRE). PDF/DJVU (KOPT) is a non-goal.

## Problem

Thai script is written without spaces between words. KOReader's EPUB engine delegates line breaking to libunibreak, which implements UAX #14. Thai code points are class `SA` ("South-East Asian"), and UAX #14 explicitly defers segmentation of these scripts to an external dictionary-based segmenter. libunibreak has none. Word selection in crengine walks characters looking for whitespace or punctuation, which never appears inside a Thai run.

Observable effects today:

- Thai paragraphs break only at explicit spaces or `<br/>`. A space-less Thai line wraps as one long unbreakable run, usually overflowing the text area.
- Tap-to-select and dictionary lookup select the entire run instead of a single word.

Evidence:
- `base/thirdparty/kpvcrlib/crengine/crengine/src/textlang.cpp:59-112` — hyphenation/language table has no `th` entry.
- `base/thirdparty/libunibreak/CMakeLists.txt:24` — libunibreak 6.1, no SEA segmentation.
- No `libthai`, no ICU `BreakIterator`, no `ubrk_`/`th_brk` symbols in the tree.

## Goals

1. Break lines at word boundaries inside Thai runs in the EPUB engine.
2. Select a single Thai word on tap (drives dictionary lookup, highlight, translate).
3. Let the user extend the segmenter's vocabulary with a plain-text word list, editable both by hand and from a long-press menu on selected text.
4. Expose a global on/off toggle in the typography menu.
5. Activate automatically when Thai characters are present; do not require `lang="th"` on the document.

## Non-goals

- Lao, Khmer, Burmese, Javanese, and other UAX #14 class-SA scripts. libthai covers Thai only; extending to other scripts would mean swapping in ICU and is out of scope.
- PDF/DJVU (KOPT) text handling. Different pipeline; separate design if desired.
- Shipping an improved stock Thai dictionary. The stock libthai dictionary is used unchanged; user-supplied words mitigate gaps for individual readers.
- Hyphenation of Thai. Thai doesn't hyphenate; word breaks are sufficient for line breaking.

## Library choice

**libthai 0.1.29** (LGPL-2.1-or-later), plus its dependency **libdatrie 0.2.13** (LGPL-2.1-or-later).

Rejected alternatives:
- **ICU BreakIterator** — accurate and covers all SEA scripts, but adds ~30 MB of data. Disproportionate for one script on e-ink devices.
- **Pure-Lua dictionary segmenter** — avoids the native dep but costs us ongoing maintenance of both the segmenter and the word list, with worse performance.

libthai is already packaged in every mainstream Linux distro, is used by Pango and Qt, and exposes a minimal C API (`th_brk_new`, `th_brk_find_breaks`, `th_brk_delete`).

## Architecture

Three additions, all localised:

1. **Third-party deps** under `base/thirdparty/libdatrie/` and `base/thirdparty/libthai/`, wired into `koreader_thirdparty_libs.cmake` and linked into `libkoreader-cre.so` via `koreader_targets.cmake`. They follow the existing libunibreak CMake wrapper pattern (external autotools project, MONOLIBTIC-aware).
2. **Thai segmenter wrapper** inside crengine: `crengine/src/textlang_thai.cpp` + `crengine/include/textlang_thai.h`. Owns a single `ThBrk *` handle, lazy-initialized on first Thai character, rebuilds a combined trie from stock + user dict when the user-words file changes. Public API:
   - `ThaiSegmenter::findLineBreaks(const lChar32 *text, int len, std::vector<int> &positions)`
   - `ThaiSegmenter::findWordBreaks(const lChar32 *text, int len, std::vector<int> &positions)`
   - `ThaiSegmenter::setEnabled(bool)`
   - `ThaiSegmenter::reload()` — re-reads user dict and rebuilds trie.
3. **Integration at two existing crengine hooks** (no new architecture):
   - Line breaking: post-pass in `lvtextfm.cpp::copyText` after the libunibreak loop.
   - Word selection: pre-check in the word-boundary walker used by `ldomDocument::selectWord` and `lvdocview`'s word stepping.

No modification to libunibreak. No new CRE language (libthai is driven by script detection, not by `TextLangCfg`).

## Line-breaking integration

In `crengine/src/lvtextfm.cpp::copyText`, after the existing libunibreak character loop that fills `m_flags[]` and before paragraph shaping:

1. If `ThaiSegmenter::isEnabled()` is false, skip.
2. Scan `m_text[0..m_length)` for maximal runs of Thai characters (`cp >= 0x0E00 && cp <= 0x0E7F`).
3. For each run of length ≥ 2, call `ThaiSegmenter::findLineBreaks(run_ptr, run_len, positions)`.
4. For each returned position `p` (1-based, relative to the run, excluding the end-of-run sentinel):
   - Set `m_flags[run_start + p - 1] |= LCHAR_ALLOW_WRAP_AFTER`.
   - Clear `LCHAR_DEPRECATED_WRAP_AFTER` at the same index so a nowrap container doesn't accidentally suppress it. A segmented Thai break is treated as equivalent to a natural word space.

Why a post-pass rather than a per-character `lb_char_sub`: libunibreak's `lb_char_sub` hook runs *before* libunibreak sees the character, which means we'd have to substitute a zero-width space to inject a break. That interferes with shaping (HarfBuzz clustering on combining marks such as Thai tone marks) and with the fallback-font logic in `xtext.cpp`. Rewriting `m_flags` after libunibreak is side-effect-free.

Cost: one `th_brk_find_breaks` per Thai run per reflow. libthai segments > 10 MB/s on desktop, > 1 MB/s on Kindle-era ARM; a typical Thai page is < 3 KB, so overhead is ≪ 1 ms per reflow.

## Word-selection integration

Two call sites in crengine walk characters looking for word boundaries:

1. **`ldomDocument::selectWord(x, y)`** — invoked from `cre.cpp::getWordFromPosition` (tap-to-select, dictionary lookup, highlight).
2. **`lvdocview` word stepping** — invoked from dragged highlight extension and find-next-word.

Both use the same underlying logic: from the touched/current index, walk back until a whitespace/punctuation char, walk forward until the next whitespace/punctuation char. The fix is the same in both places: if the character at the touch/current index is Thai, consult the segmenter instead.

Implementation:

```cpp
// pseudocode inside the existing walker
if (isThai(text[i])) {
    int run_start = findThaiRunStart(text, i);
    int run_end   = findThaiRunEnd(text, i);
    std::vector<int> boundaries;
    ThaiSegmenter::findWordBreaks(text + run_start, run_end - run_start, boundaries);
    // snap selection to [boundary_before(i), boundary_after(i))
}
```

`th_brk_find_breaks` emits the same positions for both line and word breaks; the distinction only matters when we'd otherwise count surrounding punctuation as part of the word, which is already handled by the existing walker logic once we narrow to the Thai run.

Dictionary lookup needs no changes: it consumes the resulting selected-word string.

## User dictionary management

**Source file:** `settings/thai_user_words.txt`.
Format: UTF-8, one word per line, `#` starts a comment, blank lines ignored. Created on first run (once the segmenter is initialized) with a short header explaining the format.

**Compiled trie:** `cache/cr3cache/thai_combined_v1.tri`. The `_v1` suffix is intentional — if we change combining logic later, bump the version to invalidate old caches.

**Rebuild:** at segmenter init and on `reload()`, compare mtime of source file vs. trie. If source is newer, or trie does not exist:
1. Load stock trie from `data/thai/thbrk.tri` (shipped) into a `Trie *`.
2. For each non-empty, non-comment line of `thai_user_words.txt`, call `trie_store(trie, word, TRIE_DATA_ENTRY)`.
3. `trie_save(trie, "cache/cr3cache/thai_combined_v1.tri")`.
4. Discard the in-memory `Trie *`; libthai will `mmap` the saved file via `th_brk_new()`.

Typical rebuild: ~10 ms for a few hundred user words; ~50 ms for a few thousand. One-shot at startup or on explicit reload.

**Segmenter lifetime:** created once on first Thai character seen (lazy init saves memory for non-Thai readers), held for the process lifetime. `reload()` destroys and recreates the `ThBrk *` after rebuild.

**Concurrency:** single-threaded; CRE operations happen on the main thread. No locking.

## UI surface

All additions are at existing menu-registration sites.

1. **Typography menu → "Thai word segmentation"** (checkbox, default on).
   Persisted as CRE setting `thai-segmentation-enabled` in `cre.cfg`. When false, Sections on line breaking and word selection short-circuit to no-ops. Registered in the typography menu file.

2. **Typography menu → "Reload Thai dictionary"** (button).
   Calls `ThaiSegmenter::reload()` then re-renders the current page. For when the user has edited `thai_user_words.txt` by hand.

3. **Highlight long-press menu → "Add to Thai dictionary"**.
   Shown only when the selected text contains at least one Thai character. Appends the selected word (trimmed, deduplicated against the existing file) to `settings/thai_user_words.txt`, calls `reload()`, re-renders. Registered in `frontend/apps/reader/modules/readerhighlight.lua` alongside existing highlight actions.

Delivery is staged: items 1 and 2 ship first; item 3 follows in a second PR once the core is merged and validated. All three share a single design doc and implementation plan.

## Lua ↔ C++ bridge

New Lua bindings in `base/cre.cpp`:

- `cre.setThaiSegmentationEnabled(enabled: bool)`
- `cre.reloadThaiDictionary()`
- `cre.addThaiWord(word: string)` — convenience for the long-press menu; writes to the user file and calls reload.

All three are thin wrappers; they do not take hot-path call frequency.

## Build and packaging

- `base/thirdparty/libdatrie/CMakeLists.txt` — external autotools project, MONOLIBTIC-aware, output `libdatrie.{a,so}`.
- `base/thirdparty/libthai/CMakeLists.txt` — depends on libdatrie, configures with `--without-dict` so no stock trie gets installed into the staging path. Output `libthai.{a,so}`.
- `base/thirdparty/cmake_modules/koreader_thirdparty_libs.cmake` — add both subdirs.
- `base/thirdparty/cmake_modules/koreader_targets.cmake` — link libthai, libdatrie into `libkoreader-cre`.
- `base/CMakeLists.txt` — after libthai's build generates its stock `thbrk.tri` (via `mk_thai_dict` during its own autotools build), copy that file from the libthai build tree into `${PROJECT_BINARY_DIR}/data/thai/` so it lands in the KOReader bundle under `data/thai/`.

Expected size impact per device build: ~580 KB total (~130 KB stripped code + ~450 KB stock trie). Dynamically loaded alongside other CRE dependencies.

## Settings keys

- `cre.cfg` (CRE native settings): `thai-segmentation-enabled` (bool, default true).

No new Lua settings keys. The user word file acts as its own persistent state.

## Testing

### C++ unit tests (new)

`base/thirdparty/kpvcrlib/crengine/crengine/tests/test_thai.cpp` (following existing crengine test conventions):

- `findLineBreaks` on a fixed string returns expected positions.
- `findWordBreaks` on the same string returns the same or a superset of positions.
- Adding a novel word to the user dict (via a fixture file) causes that word to be segmented as a unit.
- Empty input, single-char input, all-ASCII input all return empty boundary arrays.
- Mixed Thai+English input: Thai runs get segmented, ASCII runs are untouched.

### Integration tests (Lua, spec/unit/)

- Render a short Thai EPUB fixture; assert that `cre` exposes break positions at expected indices (via a test-only accessor if needed).
- `getWordFromPosition` on a tap coordinate inside a Thai run returns a single segmented word, not the whole run.
- Toggling the typography setting off: the same tap selects the full run again (proves the short-circuit works).
- Adding a word via `cre.addThaiWord` and re-rendering: the word is now atomic in selection.

### Manual device test plan

Captured in this document; executed before merge on at least one e-ink target (Kindle or Kobo).

- A paragraph of plain Thai wraps with multiple lines filling the text area.
- Mixed Thai + English in the same paragraph: both segment correctly.
- Thai inside `<q>` / quotation marks: quotes remain attached, segmentation unaffected.
- Thai inside `white-space: nowrap`: if the toggle is on, Thai still breaks (word break overrides nowrap inside SA runs — same as a natural space would).
- Thai split by an inline `<br/>`: each sub-run segments independently.
- Empty Thai text node (edge case).
- Rapid toggle on/off: no crash, rendering converges.
- Add a word from long-press; reload; confirm new break.

## Risks and mitigations

- **Stock libthai dictionary is dated (~2014).** Mitigation: the user dictionary. No attempt to ship a better stock dict.
- **Cache invalidation on format changes.** Mitigation: versioned cache filename (`thai_combined_v1.tri`).
- **Memory on very weak devices.** Mitigation: lazy init on first Thai character seen — non-Thai readers pay nothing. Trie is `mmap`ed, not fully loaded.
- **libthai thread safety.** `ThBrk *` is documented as not thread-safe. Mitigation: single-threaded CRE access, no sharing across threads.
- **User dict corruption (malformed UTF-8, excessively long lines).** Mitigation: line-by-line parse with validation; skip malformed lines with a log warning, continue. Never fail segmenter init because of a bad user dict.

## Open questions

None blocking. Items deferred to implementation:

- Exact menu string i18n keys (TBD at implementation time, following existing typography menu conventions).
- Whether the long-press "Add to Thai dictionary" entry should also allow adding a multi-word phrase or is restricted to a single segmenter token. Default: allow whatever the user has selected; libthai handles compound words as single entries just fine.

## Delivery sequencing

1. **PR 1 — core:** third-party build, segmenter wrapper, line-breaking integration, word-selection integration, typography toggle, reload button, C++ unit tests, Lua integration tests.
2. **PR 2 — polish:** long-press "Add to Thai dictionary" menu entry, device-tested with a Thai reader.

Each PR is independently mergeable and useful on its own.
