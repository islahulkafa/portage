#pragma once

#include "audio/opensles_types.hpp"

namespace portage::audio {

extern "C" {

SLresult slCreateEngine(SLObjectItf *pEngine, SLuint32 numOptions,
                        const SLEngineOption *pEngineOptions, SLuint32 numInterfaces,
                        const SLInterfaceID *pInterfaceIds, const SLboolean *pInterfaceRequired)
    __attribute__((sysv_abi));
}

} // namespace portage::audio
