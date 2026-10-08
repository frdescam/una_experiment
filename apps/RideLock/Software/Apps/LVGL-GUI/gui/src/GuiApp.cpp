/**
 ******************************************************************************
 * @file    GuiApp.cpp
 * @brief   RideLock GUI entry: builds the model and shows the lock screen.
 *
 * una_lvgl_app_init() is the hook the SDK's LVGL entry point calls once LVGL
 * and the display are up (Libs/Source/AppSystem/EntryPoint/LVGL/main.cpp).
 * The objects are constructed here, not as globals, so nothing touches the
 * kernel before main() has bound it.
 ******************************************************************************
 */

#include <new>

#include "gui/Assets.hpp"
#include "gui/model/Model.hpp"
#include "gui/screens/LockScreen.hpp"

#include "SDK/GUI/LVGL/Draw.hpp"

namespace
{
alignas(Model)      uint8_t sModelStorage[sizeof(Model)];
alignas(LockScreen) uint8_t sScreenStorage[sizeof(LockScreen)];
} // namespace

/**
 * @brief LVGL's default font (LV_FONT_DEFAULT in the SDK's lv_conf.h).
 *
 * Returning the app's own face keeps LVGL's built-in Montserrat out of the
 * link. Every label sets its font explicitly, so this only backs objects
 * created without one.
 */
extern "C" const lv_font_t* una_lvgl_default_font(void)
{
    return &poppins_regular_14;
}

extern "C" void una_lvgl_app_init(void)
{
    SDK::LVGL::Draw::init();
    Model*      model  = new (sModelStorage) Model();
    LockScreen* screen = new (sScreenStorage) LockScreen(*model);
    lv_screen_load(screen->root());
}
