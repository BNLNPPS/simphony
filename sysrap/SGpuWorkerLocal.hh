#pragma once

#include "SYSRAP_API_EXPORT.hh"

/** Selects device-owned state on a dedicated GPU worker. Other callers use the
 * process-wide registry, including Geant4 threads sharing the default backend.
 * Resolve each tagged slot in its owning library.
 */
struct SYSRAP_API SGpuWorkerLocal
{
    static thread_local bool active;

    template<class Owner, class T>
    static T& get(T& process)
    {
        static thread_local T worker{};
        return active ? worker : process;
    }
};
