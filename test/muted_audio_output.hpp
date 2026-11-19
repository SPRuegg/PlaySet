#pragma once

#include <AudioToolbox/AudioToolbox.h>

OSStatus playset_test_start_muted(AudioUnit unit);
// Only the native integration-test target redirects the real backend's start call.
#define AudioOutputUnitStart playset_test_start_muted
