// narutorise - mouse camera options.
//
// The camera hook itself lives in each script module (mouse_camera_script.cpp,
// built into the AI2C DLLs). The options are defined here, once, and the
// modules read them by name.
#include <rex/cvar.h>

REXCVAR_DEFINE_BOOL(mouse_camera_direct, true, "narutorise",
                    "The mouse turns the exploration camera itself, at a fixed angle per pixel, "
                    "instead of standing in for the right stick");
REXCVAR_DEFINE_BOOL(mouse_camera_trace, false, "narutorise",
                    "Log each mouse turn of the camera (motion, offset and angles), up to 2000 lines");
