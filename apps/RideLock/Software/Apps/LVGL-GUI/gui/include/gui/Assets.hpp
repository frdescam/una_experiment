/**
 ******************************************************************************
 * @file    Assets.hpp
 * @brief   Declarations of the fonts under assets/fonts (see its README).
 ******************************************************************************
 */

#ifndef ASSETS_HPP
#define ASSETS_HPP

#include "lvgl.h"

// The generated definitions are C, so the names must not be mangled. GCC
// leaves global variables unmangled anyway; MSVC (the PC simulator) does not.
#ifdef __cplusplus
extern "C" {
#endif

// Poppins, 2 bpp. SemiBold 60 carries digits, ':' and the AM/PM letters only;
// the others cover printable ASCII.
LV_FONT_DECLARE(poppins_semibold_60);
LV_FONT_DECLARE(poppins_semibold_20);
LV_FONT_DECLARE(poppins_regular_18);
LV_FONT_DECLARE(poppins_regular_14);

#ifdef __cplusplus
} // extern "C"
#endif

#endif // ASSETS_HPP
