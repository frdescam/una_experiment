#include <gtest/gtest.h>

#include <string>

#include "RideLock/Sequence.hpp"

using namespace RideLock;

namespace
{

std::string fmt(const Sequence& s)
{
    char buf[64];
    formatSequence(s, buf, sizeof(buf));
    return buf;
}

} // namespace

TEST(Sequence, DefaultGoesRoundTheCaseClockwiseOneAndAHalfTimes)
{
    EXPECT_EQ(fmt(defaultSequence()), "L1 R1 R2 L2 L1 R1");
    Sequence parsed;
    ASSERT_TRUE(parseSequence("L1 R1 R2 L2 L1 R1", parsed));   // and it is a valid one
}

TEST(Sequence, ParsesTheUsualSpellings)
{
    for (const char* text : { "L1 R1 R2 L2", "l1r1r2l2", "L1,R1,R2,L2", "L1-R1-R2-L2", "  L1  R1 R2 L2 " }) {
        Sequence s;
        ASSERT_TRUE(parseSequence(text, s)) << text;
        EXPECT_EQ(fmt(s), "L1 R1 R2 L2") << text;
    }
}

TEST(Sequence, AcceptsTheShortestTwoSidedSequence)
{
    Sequence s;
    ASSERT_TRUE(parseSequence("L2 R1", s));
    EXPECT_EQ(s.length, 2);
    EXPECT_EQ(s[0], Button::L2);
    EXPECT_EQ(s[1], Button::R1);
}

TEST(Sequence, AcceptsEightSteps)
{
    Sequence s;
    ASSERT_TRUE(parseSequence("L1 R1 L2 R2 L1 R1 L2 R2", s));
    EXPECT_EQ(s.length, 8);
}

TEST(Sequence, RejectsWhatASleeveCouldProduceOrWhatIsNotASequence)
{
    const char* bad[] = {
        "",                          // empty
        "L1",                        // one step
        "L1 L1 R1",                  // a repeat in a row
        "L1 L2 L1",                  // one side only
        "R1 R2",                     // one side only
        "L3 R1",                     // no such button
        "X1 R1",
        "L1 R1 x",                   // trailing junk
        "L1 R",                      // truncated
        "L1 R1 L2 R2 L1 R1 L2 R2 L1",// nine steps
    };
    for (const char* text : bad) {
        Sequence s = defaultSequence();
        EXPECT_FALSE(parseSequence(text, s)) << text;
        EXPECT_EQ(fmt(s), "L1 R1 R2 L2 L1 R1") << "output touched for: " << text;
    }
    Sequence s;
    EXPECT_FALSE(parseSequence(nullptr, s));
}

TEST(Sequence, FormatTruncatesSafely)
{
    char buf[6];
    formatSequence(defaultSequence(), buf, sizeof(buf));
    EXPECT_STREQ(buf, "L1 R1");
    char none[1] = { 'x' };
    formatSequence(defaultSequence(), none, sizeof(none));
    EXPECT_EQ(none[0], '\0');
}
