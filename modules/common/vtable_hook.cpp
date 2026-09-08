// filepath: modules/common/vtable_hook.cpp
// Translation unit anchor for the vtable hook helpers. The templated
// InstallVTableHook and StoreOriginal are defined inline in the header
// so callers can pick them up at the point of use. This file exists
// to keep the build happy and to give the linker a single TU that
// owns the vtable_hook.h inclusion.

#include "vtable_hook.h"

namespace omnirender {

// vtable_hook.h is header-only. This translation unit is intentionally
// empty aside from the namespace anchor; all real work happens at the
// call site where InstallVTableHook is invoked.

}  // namespace omnirender
