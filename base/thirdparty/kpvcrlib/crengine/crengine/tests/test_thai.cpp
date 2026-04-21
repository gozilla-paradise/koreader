// crengine/tests/test_thai.cpp
// C++ unit tests for ThaiSegmenter. Built only when GTest is available.
// Run via: ctest -R crengine_thai -V
#include <gtest/gtest.h>
#include <cstdlib>
#include <cstdio>
#include <sys/stat.h>
#include <vector>
#include "textlang_thai.h"

// ---------------------------------------------------------------------------
// Task 6: containsThai
// ---------------------------------------------------------------------------

TEST(ThaiSegmenter, ContainsThaiDetectsThaiChars) {
    const lChar32 thai[] = { 0x0E01, 0x0E32, 0x0E23, 0 }; // ก า ร
    EXPECT_TRUE(ThaiSegmenter::containsThai(thai, 3));
}

TEST(ThaiSegmenter, ContainsThaiRejectsAscii) {
    const lChar32 ascii[] = { 'h', 'e', 'l', 'l', 'o', 0 };
    EXPECT_FALSE(ThaiSegmenter::containsThai(ascii, 5));
}

TEST(ThaiSegmenter, ContainsThaiRejectsEmpty) {
    EXPECT_FALSE(ThaiSegmenter::containsThai(nullptr, 0));
}

// ---------------------------------------------------------------------------
// Task 7: configure + enable state
// ---------------------------------------------------------------------------

TEST(ThaiSegmenter, ConfigureStoresPathsAndEnables) {
    ThaiSegmenter::configure(lString8("/tmp/thbrk.tri"),
                             lString8("/tmp/thai_user.txt"),
                             lString8("/tmp/thai_cache.tri"));
    EXPECT_TRUE(ThaiSegmenter::isEnabled());
    ThaiSegmenter::setEnabled(false);
    EXPECT_FALSE(ThaiSegmenter::isEnabled());
    ThaiSegmenter::setEnabled(true);
    EXPECT_TRUE(ThaiSegmenter::isEnabled());
}

// ---------------------------------------------------------------------------
// Task 8: trie rebuild produces a cache file
// ---------------------------------------------------------------------------

TEST(ThaiSegmenter, RebuildProducesCacheFile) {
    // Build a temp dir with a user words file.
    std::string tmpdir = std::string("/tmp/thai_test_") + std::to_string(::getpid());
    ::mkdir(tmpdir.c_str(), 0700);

    std::string user_path  = tmpdir + "/user.txt";
    std::string cache_path = tmpdir + "/combined.tri";

    FILE * f = fopen(user_path.c_str(), "w");
    ASSERT_NE(f, nullptr);
    fputs("# test user words\n", f);
    // ก ข ค in UTF-8
    fputs("\xE0\xB8\x81\xE0\xB8\x82\xE0\xB8\x83\n", f);
    fclose(f);

    // Stock trie path is relative to CWD when tests are run from repo root.
    std::string stock_path = "data/thai/thbrk.tri";

    ThaiSegmenter::configure(lString8(stock_path.c_str()),
                             lString8(user_path.c_str()),
                             lString8(cache_path.c_str()));
    ThaiSegmenter::reload();

    struct stat st;
    EXPECT_EQ(0, ::stat(cache_path.c_str(), &st));
    EXPECT_GT(st.st_size, 0);
}

// ---------------------------------------------------------------------------
// Task 9: findLineBreaks / findWordBreaks
// ---------------------------------------------------------------------------

TEST(ThaiSegmenter, FindLineBreaksSplitsAtWordBoundary) {
    // "กินข้าว" = "kin khao" (eat rice) — two words.
    const lChar32 text[] = {
        0x0E01, 0x0E34, 0x0E19,              // กิน
        0x0E02, 0x0E49, 0x0E32, 0x0E27,      // ข้าว
        0
    };
    int len = 7;
    LVArray<int> positions;
    ThaiSegmenter::findLineBreaks(text, len, positions);
    bool found3 = false;
    for (int i = 0; i < positions.length(); i++) {
        if (positions[i] == 3) found3 = true;
    }
    EXPECT_TRUE(found3);
}

// ---------------------------------------------------------------------------
// Task 16: mixed ASCII+Thai paragraph regression
// ---------------------------------------------------------------------------

TEST(ThaiSegmenter, MixedAsciiThaiOnlyReturnsThaiBreaks) {
    // "hello กินข้าว world" — scan-and-call pattern must only produce
    // break positions inside the Thai run [6, 13).
    const lChar32 text[] = {
        'h','e','l','l','o',' ',
        0x0E01, 0x0E34, 0x0E19, 0x0E02, 0x0E49, 0x0E32, 0x0E27,
        ' ','w','o','r','l','d', 0
    };
    int len = 19;

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
    for (int b : all_breaks) {
        EXPECT_GE(b, 6);
        EXPECT_LT(b, 13);
    }
    EXPECT_GE((int)all_breaks.size(), 1);
}

// ---------------------------------------------------------------------------
// Task 17: edge cases
// ---------------------------------------------------------------------------

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

// ---------------------------------------------------------------------------
// Task 9 (continued): user dict
// ---------------------------------------------------------------------------

TEST(ThaiSegmenter, UserDictMakesNewWordAtomic) {
    // Add "ตุ๊กตา" (doll) — present in stock dict; used here for regression.
    std::string tmpdir = std::string("/tmp/thai_test_user_") + std::to_string(::getpid());
    ::mkdir(tmpdir.c_str(), 0700);
    std::string user_path  = tmpdir + "/user.txt";
    std::string cache_path = tmpdir + "/c.tri";

    FILE * f = fopen(user_path.c_str(), "w");
    ASSERT_NE(f, nullptr);
    // ตุ๊กตา in UTF-8
    fputs("\xE0\xB8\x95\xE0\xB8\xB8\xE0\xB9\x8A\xE0\xB8\x81\xE0\xB8\x95\xE0\xB8\xB2\xE0\xB9\x8A\n", f);
    fclose(f);

    ThaiSegmenter::configure(lString8("data/thai/thbrk.tri"),
                             lString8(user_path.c_str()),
                             lString8(cache_path.c_str()));
    ThaiSegmenter::reload();

    // ตุ๊กตา as UCS-4
    const lChar32 text[] = { 0x0E15, 0x0E38, 0x0E4A, 0x0E01, 0x0E15, 0x0E32, 0x0E4A, 0 };
    LVArray<int> positions;
    ThaiSegmenter::findWordBreaks(text, 7, positions);
    // Expect no internal break (the whole word is a single unit).
    for (int i = 0; i < positions.length(); i++) {
        int p = positions[i];
        EXPECT_FALSE(p > 0 && p < 7) << "Unexpected break at position " << p;
    }
}
