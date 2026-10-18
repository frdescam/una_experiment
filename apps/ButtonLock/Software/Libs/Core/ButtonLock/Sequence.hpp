/**
 ******************************************************************************
 * @file    Sequence.hpp
 * @brief   The four watch buttons, and the button sequence that unlocks.
 *
 * Pure C++ with no SDK dependency: the service, the GUI and the host tests
 * all link it. Buttons have UNA's names for them (the quick-start guide's, and
 * SDK::Message::EventButton's); the SDK's GUI codes name them by position:
 *
 *      UP      top left     (L1)       SELECT  top right     (R1)
 *      DOWN    bottom left  (L2)       BACK    bottom right  (R2)
 ******************************************************************************
 */

#ifndef BUTTONLOCK_SEQUENCE_HPP
#define BUTTONLOCK_SEQUENCE_HPP

#include <cstddef>
#include <cstdint>

namespace ButtonLock
{

enum class Button : uint8_t {
    Up     = 0,
    Down   = 1,
    Select = 2,
    Back   = 3,
};

inline constexpr size_t kButtonCount = 4;

/// Shortest and longest sequence accepted, and the fewest different buttons
/// it must use. Every two-step code, and a code on two neighbouring buttons
/// (UP SELECT UP, which a cuff rocking across the top row produces), unlocks
/// by accident hundreds to thousands of times per 1000 h even in
/// docs/sequence-study.md's milder noise. Two diagonal buttons fare better in
/// the models, but where a real cuff's edge lies is unknown, so every code on
/// two buttons is refused. Three different buttons always span both sides.
/// Eight steps is a buffer size.
inline constexpr size_t kMinSequence = 3;
inline constexpr size_t kMaxSequence = 8;
inline constexpr size_t kMinButtons  = 3;

struct Sequence {
    uint8_t length = 0;
    Button  steps[kMaxSequence] {};

    Button operator[](size_t i) const { return steps[i]; }
};

/// "UP", "DOWN", "SELECT" or "BACK".
const char* buttonName(Button b);

/// The default unlock: one lap of the case, clockwise from the top left (UP
/// SELECT BACK DOWN). Every step moves to the neighbouring button, and it
/// needs all four, which a cuff reaching two or three of them never manages
/// at any rate of pressing. In docs/sequence-study.md, the worst noise model
/// opens it about 20 times per 1000 h at its harshest and about once at a
/// level where the unlocked watch opens one activity an hour; each step more
/// or less moves that about tenfold. The lock screen lights the next button,
/// so it need not be memorised.
Sequence defaultSequence();

/**
 * @brief Parse a sequence such as "UP SELECT BACK DOWN".
 *
 * Accepts UP, DOWN, SELECT and BACK in any case, and the position names L1,
 * L2, R1 and R2 that earlier versions used, separated by spaces, commas,
 * dashes or nothing ("upselect", "l1r1"). Rejected, leaving @p out untouched:
 *   - anything else in the text;
 *   - fewer than kMinSequence or more than kMaxSequence steps;
 *   - the same button twice in a row, which one stuck or bouncing button can
 *     produce on its own;
 *   - fewer than kMinButtons different buttons (so both sides are used): a
 *     sleeve pressing one side, or rocking across two buttons, produces
 *     those.
 *
 * @return true when @p text was valid and @p out was written.
 */
bool parseSequence(const char* text, Sequence& out);

/// Write "UP SELECT BACK DOWN" into @p buf (always NUL-terminated when size
/// > 0). A step that does not fit is left out whole, never cut.
void formatSequence(const Sequence& seq, char* buf, size_t size);

} // namespace ButtonLock

#endif // BUTTONLOCK_SEQUENCE_HPP
