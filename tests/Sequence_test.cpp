#include <gtest/gtest.h>

#include <string>

#include "ButtonLock/Sequence.hpp"

using namespace ButtonLock;

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
    EXPECT_EQ(fmt(defaultSequence()), "UP SELECT BACK DOWN UP SELECT");
    Sequence parsed;
    ASSERT_TRUE(parseSequence(fmt(defaultSequence()).c_str(), parsed));   // and it is a valid one
    EXPECT_EQ(fmt(parsed), fmt(defaultSequence()));
}

TEST(Sequence, NamesAreUnasOwn)
{
    EXPECT_STREQ(buttonName(Button::Up), "UP");
    EXPECT_STREQ(buttonName(Button::Down), "DOWN");
    EXPECT_STREQ(buttonName(Button::Select), "SELECT");
    EXPECT_STREQ(buttonName(Button::Back), "BACK");
}

TEST(Sequence, ParsesTheUsualSpellings)
{
    for (const char* text : { "UP SELECT BACK DOWN", "up select back down", "Up,Select,Back,Down",
                              "UP-SELECT-BACK-DOWN", "upselectbackdown", "  UP  SELECT\tBACK DOWN " }) {
        Sequence s;
        ASSERT_TRUE(parseSequence(text, s)) << text;
        EXPECT_EQ(fmt(s), "UP SELECT BACK DOWN") << text;
    }
}

TEST(Sequence, StillParsesThePositionNamesOfEarlierVersions)
{
    // A config written for the first version (L1 top left, R1 top right,
    // L2 bottom left, R2 bottom right) keeps working, mixed or not.
    for (const char* text : { "L1 R1 R2 L2", "l1r1r2l2", "L1,R1,R2,L2", "L1-R1-R2-L2", "UP R1 back L2" }) {
        Sequence s;
        ASSERT_TRUE(parseSequence(text, s)) << text;
        EXPECT_EQ(fmt(s), "UP SELECT BACK DOWN") << text;
    }
}

TEST(Sequence, AcceptsTheShortestTwoSidedSequence)
{
    Sequence s;
    ASSERT_TRUE(parseSequence("DOWN SELECT", s));
    EXPECT_EQ(s.length, 2);
    EXPECT_EQ(s[0], Button::Down);
    EXPECT_EQ(s[1], Button::Select);
}

TEST(Sequence, AcceptsEightSteps)
{
    Sequence s;
    ASSERT_TRUE(parseSequence("SELECT DOWN SELECT DOWN SELECT DOWN SELECT DOWN", s));
    EXPECT_EQ(s.length, 8);
    EXPECT_EQ(fmt(s), "SELECT DOWN SELECT DOWN SELECT DOWN SELECT DOWN");   // the longest text: 47
}

TEST(Sequence, RejectsWhatASleeveCouldProduceOrWhatIsNotASequence)
{
    const char* bad[] = {
        "",                          // empty
        "UP",                        // one step
        "UP UP SELECT",              // a repeat in a row
        "UP L1 SELECT",              // the same button twice, under two names
        "UP DOWN UP",                // one side only
        "SELECT BACK",               // one side only
        "L3 R1",                     // no such button
        "LEFT RIGHT",
        "UPS SELECT",                // not a name
        "UP SELECT x",               // trailing junk
        "UP SELEC",                  // truncated
        "UP SELECT DOWN BACK UP SELECT DOWN BACK UP",   // nine steps
    };
    const std::string before = fmt(defaultSequence());
    for (const char* text : bad) {
        Sequence s = defaultSequence();
        EXPECT_FALSE(parseSequence(text, s)) << text;
        EXPECT_EQ(fmt(s), before) << "output touched for: " << text;
    }
    Sequence s;
    EXPECT_FALSE(parseSequence(nullptr, s));
}

TEST(Sequence, FormatLeavesOutWhatDoesNotFitWhole)
{
    Sequence s;
    ASSERT_TRUE(parseSequence("UP SELECT BACK", s));
    char buf[12];                    // "UP SELECT" and its NUL fit, " BACK" does not
    formatSequence(s, buf, sizeof(buf));
    EXPECT_STREQ(buf, "UP SELECT");
    char none[1] = { 'x' };
    formatSequence(s, none, sizeof(none));
    EXPECT_EQ(none[0], '\0');
}
