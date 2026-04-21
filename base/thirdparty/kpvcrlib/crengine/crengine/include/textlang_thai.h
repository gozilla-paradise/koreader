#ifndef TEXTLANG_THAI_H
#define TEXTLANG_THAI_H

#include "lvtypes.h"
#include "lvstring.h"
#include "lvarray.h"

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
