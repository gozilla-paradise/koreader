// crengine/src/textlang_thai.cpp
// Thin wrapper around libthai for Thai word/line segmentation.
// See crengine/include/textlang_thai.h for the public API.
//
// Build note: registered in crengine/CMakeLists.txt in CRENGINE_SOURCES,
// next to textlang.cpp.
//
// API deviations from the original plan (discovered from actual libthai 0.1.29 source):
//  - th_brk_wc_find_breaks() does NOT exist in libthai 0.1.29; th_brk_find_breaks()
//    takes const thchar_t* (TIS-620 bytes). UCS-4 input is converted via th_uni2tis().
//  - TRIE_DATA_READ does NOT exist in libdatrie 0.2.13; we use (TrieData)1 instead.
//  - The stock trie's AlphaMap covers TIS-620 bytes (0xA0-0xFF), so user words must
//    be stored with TIS-620 AlphaChar values — not raw UCS-4 code points.

#include "../include/textlang_thai.h"
#include "../include/lvstring.h"

#include <sys/stat.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <vector>

// libthai / libdatrie headers come from the staging include tree at build time.
#include <thai/thbrk.h>
#include <thai/thwchar.h>   // th_uni2tis(), thchar_t, thwchar_t
#include <datrie/trie.h>
#include <datrie/alpha-map.h>  // ALPHA_CHAR_ERROR

// ---------------------------------------------------------------------------
// Module-level state
// ---------------------------------------------------------------------------

namespace {

bool      s_enabled    = true;
bool      s_configured = false;
lString8  s_stock_path;
lString8  s_user_path;
lString8  s_cache_path;
ThBrk *   s_thbrk     = nullptr;

// ---------------------------------------------------------------------------
// Internal helpers — Task 8
// ---------------------------------------------------------------------------

time_t fileMtime(const char * path) {
    struct stat st;
    if (::stat(path, &st) != 0) return 0;
    return st.st_mtime;
}

// Inline TIS-620 conversion: th_uni2tis() from <thai/thwchar.h>.
inline thchar_t ucs4ToTis(lChar32 cp) {
    return th_uni2tis((thwchar_t)cp);
}

// Build combined trie = stock trie + user words file.
// User word lines are UTF-8; we convert to TIS-620 AlphaChar to match the
// stock trie's AlphaMap (which covers TIS-620 byte range 0xA0..0xFF).
// Returns true on success; writes result to s_cache_path.
bool buildCombinedTrie() {
    Trie * trie = trie_new_from_file(s_stock_path.c_str());
    if (!trie) {
        // Stock trie unavailable — create a minimal trie over the TIS-620 Thai range
        // so user words are still honored.
        AlphaMap * alpha_map = alpha_map_new();
        if (!alpha_map) return false;
        alpha_map_add_range(alpha_map, (AlphaChar)0xA0, (AlphaChar)0xFF);
        trie = trie_new(alpha_map);
        alpha_map_free(alpha_map);
        if (!trie) return false;
    }

    FILE * f = fopen(s_user_path.c_str(), "r");
    if (f) {
        char buf[512];
        while (fgets(buf, sizeof(buf), f)) {
            char * p = buf;
            while (*p == ' ' || *p == '\t') p++;
            if (*p == '#' || *p == '\n' || *p == '\0') continue;
            size_t n = strlen(p);
            while (n > 0 && (p[n-1] == '\n' || p[n-1] == '\r')) p[--n] = '\0';
            if (n == 0) continue;

            // Convert UTF-8 -> UCS-4 -> TIS-620 as AlphaChar.
            lString32 word = Utf8ToUnicode(lString8(p));
            int wlen = word.length();
            if (wlen == 0 || wlen > 255) continue;

            AlphaChar achars[256];
            bool valid = true;
            for (int i = 0; i < wlen; i++) {
                thchar_t tc = ucs4ToTis(word[i]);
                if (tc == THCHAR_ERR) { valid = false; break; } // not mappable to TIS-620
                achars[i] = (AlphaChar)tc;
            }
            if (!valid) continue;
            achars[wlen] = ALPHA_CHAR_ERROR;

            // TRIE_DATA_READ does not exist in libdatrie 0.2.13; use 1 as data value.
            trie_store(trie, achars, (TrieData)1);
        }
        fclose(f);
    }

    bool ok = (trie_save(trie, s_cache_path.c_str()) == 0);
    trie_free(trie);
    return ok;
}

// Ensure the segmenter handle is valid; (re)build if cache is stale.
bool ensureTrie() {
    if (!s_configured) return false;
    time_t user_mt  = fileMtime(s_user_path.c_str());
    time_t cache_mt = fileMtime(s_cache_path.c_str());
    if (cache_mt == 0 || user_mt > cache_mt) {
        if (!buildCombinedTrie()) return false;
    }
    if (!s_thbrk) {
        s_thbrk = th_brk_new(s_cache_path.c_str());
    }
    return s_thbrk != nullptr;
}

// ---------------------------------------------------------------------------
// Internal helpers — Task 9
// ---------------------------------------------------------------------------

// Core break-finding logic shared by findLineBreaks and findWordBreaks.
//
// th_brk_find_breaks() takes const thchar_t* (TIS-620 single bytes) and
// returns the count of break positions written into pos[].  pos[] values
// are 0-based offsets where a break may occur *before* that character —
// i.e. the caller may insert a break before in[pos[i]].
//
// Our public contract: position p means "break between char p-1 and p"
// (1-based, relative to text). The returned pos[] values already equal
// our "p" — pass them through directly after bounds-checking.
//
// Characters that don't map to TIS-620 (th_uni2tis returns 0) are replaced
// with a TIS-620 space (0x20) so libthai can still segment around them
// without a buffer overrun or misbehaviour.
void findBreaksInternal(const lChar32 * text, int len, LVArray<int> & positions) {
    positions.clear();
    if (!ThaiSegmenter::isEnabled() || len <= 1) return;
    if (!ensureTrie()) return;

    std::vector<thchar_t> in((size_t)(len + 1));
    for (int i = 0; i < len; i++) {
        thchar_t tc = ucs4ToTis(text[i]);
        in[(size_t)i] = (tc != THCHAR_ERR) ? tc : (thchar_t)0x20;
    }
    in[(size_t)len] = '\0';

    std::vector<int> out((size_t)(len + 1));
    int n = th_brk_find_breaks(s_thbrk, in.data(), out.data(), out.size());
    for (int i = 0; i < n; i++) {
        if (out[i] > 0 && out[i] < len) positions.add(out[i]);
    }
}

} // namespace

// ---------------------------------------------------------------------------
// Public API — containsThai (Task 6)
// ---------------------------------------------------------------------------

bool ThaiSegmenter::containsThai(const lChar32 * text, int len) {
    for (int i = 0; i < len; i++) {
        if (text[i] >= 0x0E00 && text[i] <= 0x0E7F)
            return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Public API — configure + enable state (Task 7)
// ---------------------------------------------------------------------------

void ThaiSegmenter::configure(const lString8 & stock, const lString8 & user,
                              const lString8 & cache) {
    s_stock_path = stock;
    s_user_path  = user;
    s_cache_path = cache;
    s_configured = true;
}

void ThaiSegmenter::setEnabled(bool e) { s_enabled = e; }
bool ThaiSegmenter::isEnabled()        { return s_enabled && s_configured; }

// ---------------------------------------------------------------------------
// Public API — trie rebuild (Task 8)
// ---------------------------------------------------------------------------

void ThaiSegmenter::reload() {
    if (s_thbrk) {
        th_brk_delete(s_thbrk);
        s_thbrk = nullptr;
    }
    // Delete cache so ensureTrie() forces a full rebuild on next call.
    if (s_configured) ::unlink(s_cache_path.c_str());
    ensureTrie();
}

void ThaiSegmenter::uninit() {
    if (s_thbrk) {
        th_brk_delete(s_thbrk);
        s_thbrk = nullptr;
    }
}

// ---------------------------------------------------------------------------
// Public API — findLineBreaks / findWordBreaks / addUserWord (Task 9)
// ---------------------------------------------------------------------------

void ThaiSegmenter::findLineBreaks(const lChar32 * text, int len,
                                   LVArray<int> & positions) {
    findBreaksInternal(text, len, positions);
}

void ThaiSegmenter::findWordBreaks(const lChar32 * text, int len,
                                   LVArray<int> & positions) {
    // libthai uses the same dictionary boundaries for both line and word breaking;
    // surrounding punctuation is handled by callers once they have the run.
    findBreaksInternal(text, len, positions);
}

bool ThaiSegmenter::addUserWord(const lString32 & word) {
    if (word.empty() || !s_configured) return false;
    lString8 utf8_word = UnicodeToUtf8(word);

    // Deduplicate before appending.
    FILE * rf = fopen(s_user_path.c_str(), "r");
    if (rf) {
        char buf[512];
        while (fgets(buf, sizeof(buf), rf)) {
            size_t n = strlen(buf);
            while (n > 0 && (buf[n-1] == '\n' || buf[n-1] == '\r')) buf[--n] = '\0';
            if (strcmp(buf, utf8_word.c_str()) == 0) {
                fclose(rf);
                return true; // already present — nothing to do
            }
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
