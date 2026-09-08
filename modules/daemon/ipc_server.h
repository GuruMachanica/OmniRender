// filepath: modules/daemon/ipc_server.h
#pragma once

#include "../common/ring_buffer.h"

namespace omnirender::daemon {

int  InitializeIpcServer();
void ShutdownIpcServer();

// Try to consume one ready frame slot. On success returns true and
// out_slot points to a slot that has been moved to SlotState::Processing
// (so the producer cannot reclaim it while it is being read). The caller
// MUST call ReleaseFrame(slot) when done.
bool ConsumeFrame(omnirender::FrameSlot*& out_slot);

// Return a slot to the FREE state. The producer will reuse it.
void ReleaseFrame(omnirender::FrameSlot& slot);

// Return the producer's GPU adapter LUID from the ring control block.
uint64_t GetRingAdapterLuid();

}  // namespace omnirender::daemon
