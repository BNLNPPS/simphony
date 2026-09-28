#pragma once
/**
s_windows.h : POSIX compatibility for the MSVC build
======================================================

The core packages are POSIX-only in their utility layer. This header supplies
the handful of facilities MSVC lacks, so the call sites keep their POSIX
spelling and the Linux build is untouched: everything here is inside
``#if defined(_MSC_VER)``.

Covered:

* ``unistd.h`` entry points that exist in the Windows CRT under an underscore
  (``getcwd``, ``chdir``, ``getpid``, ``popen``, ``pclose``)
* ``mkdir``, whose POSIX mode argument the Windows CRT does not take
* the ``sys/wait.h`` status macros, which on Windows describe the value
  ``_pclose`` already returns directly
* ``gettimeofday``

Directory iteration (``opendir``/``readdir``) is deliberately not shimmed:
those call sites use ``std::filesystem`` instead, which is available on both
platforms and removes the POSIX dependency rather than papering over it.

**/

#if defined(_MSC_VER)

// Ahead of windows.h: winsock2.h owns struct timeval, and windows.h defines
// min/max macros that collide with std::min/std::max.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <psapi.h>

// windows.h claims common identifiers as macros; the tree uses them all as
// plain C++ names (near/far in csg_intersect_leaf_box3.h, CONST in SPMT.h,
// MOD_SHIFT/MOD_CONTROL/MOD_ALT enums in SGLM_Modifiers.h) and nothing here
// needs the Windows meanings.
#undef near
#undef far
#undef CONST
#undef MOD_SHIFT
#undef MOD_CONTROL
#undef MOD_ALT

#include <direct.h>
#include <io.h>
#include <process.h>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

// GetProcessMemoryInfo, used for the /proc/self/status replacement in sproc.h.
#pragma comment(lib, "psapi.lib")
// htonl/ntohl (winsock2.h), used by the net_hdr byte-order packing in NPU.hh.
#pragma comment(lib, "ws2_32.lib")

#define getcwd _getcwd
#define chdir  _chdir
#define popen  _popen
#define pclose _pclose
#define getpid _getpid

typedef int mode_t;
typedef std::intptr_t ssize_t;

// MAX_PATH is Windows' equivalent limit; the call sites size stack buffers
// with it.
#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif

// POSIX permission bits. Windows has no such concept and the CRT mkdir takes
// no mode, so these exist only to let the mode expressions at the call sites
// compile; the values are never applied.
#ifndef S_IRWXU
#define S_IRWXU 0
#endif
#ifndef S_IRUSR
#define S_IRUSR 0
#endif
#ifndef S_IWUSR
#define S_IWUSR 0
#endif
#ifndef S_IXUSR
#define S_IXUSR 0
#endif
#ifndef S_IRGRP
#define S_IRGRP 0
#endif
#ifndef S_IXGRP
#define S_IXGRP 0
#endif
#ifndef S_IROTH
#define S_IROTH 0
#endif
#ifndef S_IXOTH
#define S_IXOTH 0
#endif

// POSIX mkdir takes a permission mode; the Windows CRT form takes none.
// Windows has no POSIX permission bits to apply, so the mode is dropped.
#define mkdir(path, mode) _mkdir(path)

// stat-mode classification macros; the CRT supplies the flag bits only.
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif
#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif

// strndup is POSIX; the CRT has no counterpart.
inline char* s_windows_strndup(const char* s, size_t n)
{
    size_t len = 0 ;
    while(len < n && s[len] != '\0') len++ ;
    char* d = static_cast<char*>(malloc(len + 1));
    if(d != nullptr)
    {
        memcpy(d, s, len);
        d[len] = '\0' ;
    }
    return d ;
}
#define strndup s_windows_strndup

// _pclose returns the command's exit status directly, with no wait(2) status
// word to decode, so the extraction macros are identities.
#ifndef WIFEXITED
#define WIFEXITED(status) (1)
#endif
#ifndef WEXITSTATUS
#define WEXITSTATUS(status) (status)
#endif
#ifndef WIFSIGNALED
#define WIFSIGNALED(status) (0)
#endif
#ifndef WTERMSIG
#define WTERMSIG(status) (0)
#endif

/**
s_windows_gettimeofday
-----------------------

The timezone argument is obsolete in POSIX and ignored here, matching every
call site in this tree, which passes NULL. The epoch conversion is the
standard one: the Windows FILETIME epoch is 1601-01-01, 11644473600 seconds
before the Unix epoch, counted in 100-nanosecond ticks.

**/

inline int s_windows_gettimeofday(struct timeval* tv, void* /*tz*/)
{
    if (tv == nullptr) return -1;

    FILETIME ft;
    GetSystemTimeAsFileTime(&ft);

    ULARGE_INTEGER t;
    t.LowPart = ft.dwLowDateTime;
    t.HighPart = ft.dwHighDateTime;

    const std::uint64_t EPOCH_DIFF_100NS = 116444736000000000ULL;
    std::uint64_t ticks = t.QuadPart - EPOCH_DIFF_100NS;

    tv->tv_sec = static_cast<long>(ticks / 10000000ULL);
    tv->tv_usec = static_cast<long>((ticks % 10000000ULL) / 10ULL);
    return 0;
}

#define gettimeofday s_windows_gettimeofday

#endif
