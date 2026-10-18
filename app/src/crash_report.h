// narutorise - ReXGlue Recompiled Project
//
// Crash report of a guest fault, in the log (crash_report.cpp): the vectored
// exception handler after the SDK's writes, for a fault the SDK does not
// resume (the "Unhandled guest access violation" line), the guest thread, its
// registers and the chain of return addresses of the guest stack with the
// repetitions counted, so a crash explains itself without a dump. The unit
// also holds the probe of the object link chain (8266AE68) and the log line of
// each world load (8264C358).

#pragma once

namespace narutorise::crash_report {
// Installs the handler once; from OnPostSetup, after the SDK's own handler.
void Install();
}  // namespace narutorise::crash_report
