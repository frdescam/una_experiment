/**
 ******************************************************************************
 * @file    Sequence.cpp
 * @brief   Button names and unlock-sequence parsing.
 ******************************************************************************
 */

#include "RideLock/Sequence.hpp"

namespace RideLock
{

namespace
{

bool isSeparator(char c)
{
    return c == ' ' || c == ',' || c == '-' || c == '\t';
}

bool isLeft(Button b)
{
    return b == Button::L1 || b == Button::L2;
}

} // namespace

const char* buttonName(Button b)
{
    switch (b) {
        case Button::L1: return "L1";
        case Button::L2: return "L2";
        case Button::R1: return "R1";
        case Button::R2: return "R2";
    }
    return "??";
}

Sequence defaultSequence()
{
    Sequence seq;
    seq.length   = 6;
    seq.steps[0] = Button::L1;
    seq.steps[1] = Button::R1;
    seq.steps[2] = Button::R2;
    seq.steps[3] = Button::L2;
    seq.steps[4] = Button::L1;
    seq.steps[5] = Button::R1;
    return seq;
}

bool parseSequence(const char* text, Sequence& out)
{
    if (text == nullptr) {
        return false;
    }

    Sequence seq;
    const char* p = text;

    while (*p != '\0') {
        if (isSeparator(*p)) {
            ++p;
            continue;
        }

        const char side = *p++;
        const char row  = *p;
        if (row == '\0') {
            return false;
        }
        ++p;

        Button b;
        if ((side == 'L' || side == 'l') && row == '1') {
            b = Button::L1;
        } else if ((side == 'L' || side == 'l') && row == '2') {
            b = Button::L2;
        } else if ((side == 'R' || side == 'r') && row == '1') {
            b = Button::R1;
        } else if ((side == 'R' || side == 'r') && row == '2') {
            b = Button::R2;
        } else {
            return false;
        }

        if (seq.length == kMaxSequence) {
            return false;
        }
        if (seq.length > 0 && seq.steps[seq.length - 1] == b) {
            return false;
        }
        seq.steps[seq.length++] = b;
    }

    if (seq.length < kMinSequence) {
        return false;
    }

    bool left  = false;
    bool right = false;
    for (size_t i = 0; i < seq.length; ++i) {
        if (isLeft(seq.steps[i])) {
            left = true;
        } else {
            right = true;
        }
    }
    if (!left || !right) {
        return false;
    }

    out = seq;
    return true;
}

void formatSequence(const Sequence& seq, char* buf, size_t size)
{
    if (buf == nullptr || size == 0) {
        return;
    }

    size_t pos = 0;
    for (size_t i = 0; i < seq.length && i < kMaxSequence; ++i) {
        const char* name = buttonName(seq.steps[i]);
        const size_t need = (i > 0 ? 1 : 0) + 2;
        if (pos + need >= size) {
            break;
        }
        if (i > 0) {
            buf[pos++] = ' ';
        }
        buf[pos++] = name[0];
        buf[pos++] = name[1];
    }
    buf[pos] = '\0';
}

} // namespace RideLock
