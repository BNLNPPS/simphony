#pragma once

#include "GpuEventPool.h"

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <climits>
#include <cmath>
#include <cstddef>
#include <limits>
#include <poll.h>
#include <stdexcept>
#include <sys/socket.h>
#include <system_error>
#include <type_traits>
#include <unistd.h>

namespace simphony::gpu_wire
{
// Private native-ABI protocol between the parent and GPU worker processes on the same Linux host.
using Clock = std::chrono::steady_clock;
using Deadline = Clock::time_point;
inline Deadline deadlineAfter(Deadline now, double seconds)
{
    using FractionalTicks = std::chrono::duration<long double, Clock::period>;
    const FractionalTicks duration = std::chrono::duration<long double>(seconds);
    const auto            ticks = std::ceil(duration.count());
    // Check the rounded count before narrowing. Deadline::max() is reserved for child-side unlimited I/O.
    if (!(ticks > 0 && ticks < (Deadline::max() - now).count()))
        throw std::invalid_argument("GPU timeout exceeds the representable clock interval");
    return now + Clock::duration{static_cast<Clock::rep>(ticks)};
}

constexpr int      Socket = 3;
constexpr uint16_t Version = 3;
enum class Type : uint16_t
{
    Ready = 1,
    Gensteps,
    Photons,
    Hits,
    Error,
    Finished
};
constexpr size_t   IoChunkBytes = 1024 * 1024;
constexpr uint64_t MaxError = 4096;
static_assert(sizeof(quad6) == 96 && sizeof(sphoton) == 64);
static_assert(std::is_trivially_copyable_v<quad6> && std::is_trivially_copyable_v<sphoton>);

inline uint64_t payloadBytes(Type type, uint64_t count)
{
    uint64_t stride;
    switch (type)
    {
    case Type::Ready:
        return 0; // count advertises the initialized helper's MaxSlot. READY has no body.
    case Type::Finished:
        if (count != 0)
            throw std::runtime_error("unexpected GPU control payload");
        return 0;
    case Type::Gensteps:
        stride = sizeof(quad6);
        break;
    case Type::Photons:
    case Type::Hits:
        stride = sizeof(sphoton);
        break;
    case Type::Error:
        if (count > MaxError)
            throw std::runtime_error("oversized GPU helper error");
        return count;
    default:
        throw std::runtime_error("unknown GPU message type");
    }
    // Both endpoints hold the records in contiguous containers with ptrdiff_t-sized distances.
    if (count > std::numeric_limits<std::ptrdiff_t>::max() / stride)
        throw std::runtime_error("GPU message size is not representable");
    return count * stride;
}

struct Header
{
    uint32_t magic{0x53475055};
    uint16_t version{Version};
    Type     type;
    int32_t  run, event;
    uint64_t count, bytes;
    int64_t  start{}, end{}, cleanup{};
    Header(Type kind, int run_id, int event_id, uint64_t elements = 0) :
        type(kind),
        run(run_id),
        event(event_id),
        count(elements),
        bytes(payloadBytes(type, count))
    {
    }
};
static_assert(sizeof(Header) == 56 && std::is_trivially_copyable_v<Header>);

class EndOfStream : public std::runtime_error
{
  public:
    EndOfStream() :
        std::runtime_error("GPU helper socket closed")
    {
    }
};

inline void waitFor(int fd, short events, Deadline deadline)
{
    for (;;)
    {
        int timeout = -1;
        if (deadline != Deadline::max())
        {
            const auto remaining = std::chrono::ceil<std::chrono::milliseconds>(deadline - Clock::now()).count();
            if (remaining <= 0)
                throw std::runtime_error("GPU helper deadline exceeded");
            timeout = static_cast<int>(std::min<int64_t>(remaining, INT_MAX));
        }
        pollfd pfd{fd, events, 0};
        int    ready = poll(&pfd, 1, timeout);
        if (ready > 0)
            return; // recv/send also report EOF and socket errors.
        if (ready < 0 && errno != EINTR)
            throw std::system_error(errno, std::generic_category(), "poll GPU helper");
    }
}

inline void readAll(int fd, void* data, size_t bytes, Deadline deadline, bool atFrameBoundary = false)
{
    const auto requested = bytes;
    char*      out = static_cast<char*>(data);
    while (bytes)
    {
        waitFor(fd, POLLIN, deadline);
        ssize_t n = recv(fd, out, std::min(bytes, IoChunkBytes), MSG_DONTWAIT);
        if (n == 0)
        {
            if (atFrameBoundary && bytes == requested)
                throw EndOfStream();
            throw std::runtime_error("truncated GPU frame");
        }
        if (n < 0)
        {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            throw std::system_error(errno, std::generic_category(), "read GPU helper");
        }
        out += n;
        bytes -= n;
    }
}

inline void writeAll(int fd, const void* data, size_t bytes, Deadline deadline)
{
    const char* in = static_cast<const char*>(data);
    while (bytes)
    {
        waitFor(fd, POLLOUT, deadline);
        ssize_t n = send(fd, in, std::min(bytes, IoChunkBytes), MSG_DONTWAIT | MSG_NOSIGNAL);
        if (n < 0)
        {
            if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
                continue;
            throw std::system_error(errno, std::generic_category(), "write GPU helper");
        }
        if (n == 0)
            throw EndOfStream();
        in += n;
        bytes -= n;
    }
}

inline void writeHeader(int fd, const Header& h, Deadline deadline)
{
    writeAll(fd, &h, sizeof(h), deadline);
}

inline Header readHeader(int fd, Deadline deadline)
{
    Header h(Type::Ready, 0, -1);
    readAll(fd, &h, sizeof(h), deadline, true);
    const bool control = h.type == Type::Ready || h.type == Type::Finished;
    if (h.magic != 0x53475055 || h.version != Version || h.run < 0 || h.event < -1 ||
        (control && h.event != -1) ||
        (h.type == Type::Ready && (h.count == 0 || h.count > INT64_MAX)) ||
        (!control && h.type != Type::Error && h.event < 0) ||
        h.bytes != payloadBytes(h.type, h.count))
        throw std::runtime_error("invalid GPU helper header");
    return h;
}

inline Header expect(int fd, Type type, int run, int event, Deadline deadline)
{
    auto h = readHeader(fd, deadline);
    if (h.run != run || h.event != event)
        throw std::runtime_error("GPU helper reply identity mismatch");
    if (h.type == Type::Error)
    {
        std::string error(h.count, '\0');
        readAll(fd, error.data(), h.bytes, deadline);
        throw std::runtime_error("GPU helper: " + error);
    }
    if (h.type != type)
        throw std::runtime_error("unexpected GPU helper reply type");
    return h;
}
} // namespace simphony::gpu_wire
