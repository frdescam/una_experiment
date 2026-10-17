/**
 ******************************************************************************
 * @file    Sequence.cpp
 * @brief   Button names and unlock-sequence parsing.
 ******************************************************************************
 */

#include "ButtonLock/Sequence.hpp"

namespace ButtonLock
{

namespace
{

bool isSeparator(char c)
{
    return c == ' ' || c == ',' || c == '-' || c == '\t';
}

/// The names parseSequence() accepts: UNA's, then the positions.
struct Token {
    const char* text;
    Button      button;
};

constexpr Token kTokens[] = {
    { "SELECT", Button::Select },
    { "DOWN",   Button::Down },
    { "BACK",   Button::Back },
    { "UP",     Button::Up },
    { "L1",     Button::Up },
    { "L2",     Button::Down },
    { "R1",     Button::Select },
    { "R2",     Button::Back },
};

char upper(char c)
{
    return (c >= 'a' && c <= 'z') ? static_cast<char>(c - 'a' + 'A') : c;
}

/// Length of @p token if @p p starts with it (in any case), else 0.
size_t match(const char* p, const char* token)
{
    size_t n = 0;
    while (token[n] != '\0') {
        if (upper(p[n]) != token[n]) {
            return 0;
        }
        ++n;
    }
    return n;
}

} // namespace

const char* buttonName(Button b)
{
    switch (b) {
        case Button::Up:     return "UP";
        case Button::Down:   return "DOWN";
        case Button::Select: return "SELECT";
        case Button::Back:   return "BACK";
    }
    return "??";
}

Sequence defaultSequence()
{
    Sequence seq;
    seq.length   = 4;
    seq.steps[0] = Button::Up;
    seq.steps[1] = Button::Select;
    seq.steps[2] = Button::Back;
    seq.steps[3] = Button::Down;
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

        size_t used = 0;
        Button b    = Button::Up;
        for (const Token& t : kTokens) {
            used = match(p, t.text);
            if (used > 0) {
                b = t.button;
                break;
            }
        }
        if (used == 0) {
            return false;
        }
        p += used;

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

    bool   used[kButtonCount] = {};
    size_t buttons            = 0;
    for (size_t i = 0; i < seq.length; ++i) {
        bool& seen = used[static_cast<size_t>(seq.steps[i])];
        if (!seen) {
            seen = true;
            ++buttons;
        }
    }
    if (buttons < kMinButtons) {
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
        size_t len = 0;
        while (name[len] != '\0') {
            ++len;
        }
        const size_t need = (i > 0 ? 1 : 0) + len;
        if (pos + need >= size) {
            break;
        }
        if (i > 0) {
            buf[pos++] = ' ';
        }
        for (size_t k = 0; k < len; ++k) {
            buf[pos++] = name[k];
        }
    }
    buf[pos] = '\0';
}

} // namespace ButtonLock
