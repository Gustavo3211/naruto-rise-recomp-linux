// narutorise - ReXGlue Recompiled Project

#include "generated/default/narutorise_init.h"

#include "narutorise_app.h"

REX_DEFINE_APP(narutorise, NarutoriseApp::Create)

// The discrete GPU on hybrid laptops (NVIDIA Optimus, AMD PowerXpress), as
// Unleashed Recompiled does: the drivers read these exports from the
// executable only, not from rexruntime.dll, which also has them.
extern "C" {
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
}
