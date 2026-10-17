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

/// Shortest and longest sequence accepted. Two steps is the floor because
/// one button alone is exactly what a sleeve produces; eight is a buffer size.
inline constexpr size_t kMinSequence = 2;
inline constexpr size_t kMaxSequence = 8;

struct Sequence {
    uint8_t length = 0;
    Button  steps[kMaxSequence] {};

    Button operator[](size_t i) const { return steps[i]; }
};

/// "UP", "DOWN", "SELECT" or "BACK".
const char* buttonName(Button b);

/// The default unlock: one and a half laps of the case, clockwise from the
/// top left (L1 R1 R2 L2 L1 R1). Every step moves to the neighbouring button
/// and uses both sides, which a cuff pressing one side cannot do; six steps
/// is what the jacket model (tests/JacketModel.hpp) needed to keep random
/// pressing on all four buttons below one false unlock per thousand hours.
/// The lock screen lights the next button, so it need not be memorised.
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
 *   - a sequence that uses only one side of the case, which a sleeve pressing
 *     that side could produce.
 *
 * @return true when @p text was valid and @p out was written.
 */
bool parseSequence(const char* text, Sequence& out);

/// Write "UP SELECT BACK DOWN" into @p buf (always NUL-terminated when size
/// > 0). A step that does not fit is left out whole, never cut.
void formatSequence(const Sequence& seq, char* buf, size_t size);

} // namespace ButtonLock

#endif // BUTTONLOCK_SEQUENCE_HPP
