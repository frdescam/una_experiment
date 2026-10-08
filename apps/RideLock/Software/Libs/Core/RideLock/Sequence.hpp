/**
 ******************************************************************************
 * @file    Sequence.hpp
 * @brief   The four watch buttons, and the button sequence that unlocks.
 *
 * Pure C++ with no SDK dependency: the service, the GUI and the host tests
 * all link it. Buttons are named after their position on the case, the same
 * names SDK/GUI/Button.hpp uses:
 *
 *      L1  top left        R1  top right   (select / start)
 *      L2  bottom left     R2  bottom right (back / lap)
 ******************************************************************************
 */

#ifndef RIDELOCK_SEQUENCE_HPP
#define RIDELOCK_SEQUENCE_HPP

#include <cstddef>
#include <cstdint>

namespace RideLock
{

enum class Button : uint8_t {
    L1 = 0,
    L2 = 1,
    R1 = 2,
    R2 = 3,
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

/// "L1", "L2", "R1" or "R2".
const char* buttonName(Button b);

/// The default unlock: one and a half laps of the case, clockwise from the
/// top left (L1 R1 R2 L2 L1 R1). Every step moves to the neighbouring button
/// and uses both sides, which a cuff pressing one side cannot do; six steps
/// is what the jacket model (tests/JacketModel.hpp) needed to keep random
/// pressing on all four buttons below one false unlock per thousand hours.
/// The lock screen lights the next button, so it need not be memorised.
Sequence defaultSequence();

/**
 * @brief Parse a sequence such as "L1 R1 R2 L2".
 *
 * Accepts L1/L2/R1/R2 in either case, separated by spaces, commas, dashes or
 * nothing ("l1r1r2l2"). Rejected, leaving @p out untouched:
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

/// Write "L1 R1 R2 L2" into @p buf (always NUL-terminated when size > 0).
void formatSequence(const Sequence& seq, char* buf, size_t size);

} // namespace RideLock

#endif // RIDELOCK_SEQUENCE_HPP
