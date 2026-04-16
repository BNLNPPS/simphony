#pragma once
/**
sproc.h
===========

Implementations of VirtualMemoryUsageMB of a process.
Migrated from former SProc.hh.

Survey usage, mostly ExecutableName::

    epsilon:opticks blyth$ opticks-fl sproc.h
    ./CSGOptiX/CSGOptiX.cc    ## ExecutableName
    ./CSG/CSGFoundry.cc       ## NOT USED : REMOVED
    ./sysrap/sproc.h
    ./sysrap/spath.h          ## spath::_ResolveToken replaces $ExecutableName


    ./sysrap/SProc.hh
    ./sysrap/SOpticksResource.cc  ## ExecutableName

    ./sysrap/CMakeLists.txt
    ./sysrap/tests/reallocTest.cc
    ./sysrap/tests/sproc_test.cc

    ./sysrap/SOpticks.cc       ## ExecutableName

    ./sysrap/SLOG.cc           ## ExecutableName
    ./sysrap/smeta.h           ## ExecutableName
    ./sysrap/SPMT.h            ## ExecutableName
    ./sysrap/SGeo.cc           ## NOT USED : COMMENTED
    ./qudarap/QPMT.hh          ## ExecutableName

    epsilon:opticks blyth$


**/

#include <cassert>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits.h>
#include <unistd.h>

struct sproc
{
    static constexpr const int32_t K = 1000; // 1024?
    static int32_t parseLine(char *line);

    static int Query(int32_t &virtual_size_kb, int32_t &resident_size_kb);

    static float VirtualMemoryUsageMB();
    static float VirtualMemoryUsageKB();
    static float ResidentSetSizeMB();
    static float ResidentSetSizeKB();

    static char *ExecutablePath(bool basename = false);
    static char *_ExecutableName();
    static bool StartsWith(const char *s, const char *q);
    static char *ExecutableName();
};

/**
sproc::parseLine
-----------------

Expects a line of the below form with digits and ending in " Kb"::

   VmSize:	  108092 kB

**/

inline int32_t sproc::parseLine(char *line)
{
    int i = strlen(line);
    const char *p = line;
    while (*p < '0' || *p > '9')
        p++;            // advance until first digit
    line[i - 3] = '\0'; // chop off the " kB"
    return std::atoi(p);
}

// https://stackoverflow.com/questions/63166/how-to-determine-cpu-and-memory-consumption-from-inside-a-process

inline int sproc::Query(int32_t &virtual_size, int32_t &resident_size)
{
    FILE *file = fopen("/proc/self/status", "r");
    char line[128];
    int found = 0;
    while (fgets(line, 128, file) != NULL)
    {
        if (strncmp(line, "VmSize:", 7) == 0)
        {
            virtual_size = parseLine(line); // value in Kb
            found += 1;
            if (found == 2)
                break;
        }
        else if (strncmp(line, "VmRSS:", 6) == 0)
        {
            resident_size = parseLine(line); // value in Kb
            found += 1;
            if (found == 2)
                break;
        }
    }
    fclose(file);
    return found == 2 ? 0 : 1;
}

inline float sproc::VirtualMemoryUsageKB()
{
    int32_t virtual_size_kb(0);
    int32_t resident_size_kb(0);
    Query(virtual_size_kb, resident_size_kb);
    return virtual_size_kb;
}
inline float sproc::ResidentSetSizeKB()
{
    int32_t virtual_size_kb(0);
    int32_t resident_size_kb(0);
    Query(virtual_size_kb, resident_size_kb);
    return resident_size_kb;
}
inline float sproc::VirtualMemoryUsageMB()
{
    int32_t virtual_size_kb(0);
    int32_t resident_size_kb(0);
    Query(virtual_size_kb, resident_size_kb);
    float size_mb = virtual_size_kb / K;
    return size_mb;
}
inline float sproc::ResidentSetSizeMB()
{
    int32_t virtual_size_kb(0);
    int32_t resident_size_kb(0);
    Query(virtual_size_kb, resident_size_kb);
    float size_mb = resident_size_kb / K;
    return size_mb;
}

/**
sproc::ExecutablePath
-----------------------

 * Linux `/proc/self/exe` implementation.

**/

inline char *sproc::ExecutablePath(bool basename)
{
    char buf[PATH_MAX];
    ssize_t len = ::readlink("/proc/self/exe", buf, sizeof(buf) - 1);
    if (len != -1)
        buf[len] = '\0';

    char *s = basename ? strrchr(buf, '/') : NULL;
    return s ? strdup(s + 1) : strdup(buf);
}

inline char *sproc::_ExecutableName()
{
    bool basename = true;
    return ExecutablePath(basename);
}

inline bool sproc::StartsWith(const char *s, const char *q)
{
    return s && q && strlen(q) <= strlen(s) && strncmp(s, q, strlen(q)) == 0;
}

/**
sproc::ExecutableName
----------------------

In embedded running with python "main" the
initial executable name is eg "python3.9".
That can be overridden with envvar OPTICKS_SCRIPT

**/

inline char *sproc::ExecutableName()
{
    char *exe0 = sproc::_ExecutableName();
    bool is_python = sproc::StartsWith(exe0, "python");
    char *script = getenv("OPTICKS_SCRIPT");
    char *exe = (is_python && script) ? script : exe0;
    return exe;
}
