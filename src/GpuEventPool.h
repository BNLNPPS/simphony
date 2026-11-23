#pragma once

#include <condition_variable>
#include <deque>
#include <exception>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>

#include "EventBuffer.h"
#include "sysrap/EventTiming.hh"

struct NP;
namespace simphony
{
struct GpuEventResult
{
    size_t               photons{};
    std::vector<sphoton> hits;
};

/** Transport one nonempty event on the calling thread's device and collect its hits.
 * A null buffer indicates that the serial recorder already prepared the inputs.
 */
GpuEventResult SimulateGpuEvent(int event_id, const EventBuffer* buffer, std::unique_ptr<NP>& input,
                                EventTimingRecorder* timing);

/** A bounded queue of complete Geant4 events served by one thread per visible GPU. */
class GpuEventPool
{
  public:
    explicit GpuEventPool(int device_count);
    ~GpuEventPool();

    /** Start a new run after the previous run has drained. */
    void BeginRun(EventTimingRecorder* master);
    /** Transfer an event at its Geant4 boundary. A full queue applies backpressure. */
    void Submit(EventBuffer buffer, size_t g4_hits, EventTimingRecorder* producer);
    /** Wait for all submissions, merge GPU timings and return hits in event order. */
    std::vector<sphoton> FinishRun(EventTimingRecorder* master);

  private:
    struct Job
    {
        EventBuffer                                     buffer;
        size_t                                          g4_hits;
        size_t                                          required_slots;
        std::optional<EventTimingRecorder::QueuedEvent> timing;
    };

    struct Device
    {
        size_t                             max_slots{};
        std::string                        uuid;
        std::optional<EventTimingRecorder> timing;
    };

    void Serve(int ordinal);
    void Stop();
    void Fail(std::exception_ptr error);

    std::vector<Device>           devices_;
    std::vector<std::thread>      threads_;
    std::deque<Job>               pending_;
    std::map<int, GpuEventResult> results_;
    std::mutex                    mutex_;
    std::condition_variable       changed_;
    size_t                        ready_{};
    size_t                        active_{};
    bool                          stopping_{};
    std::exception_ptr            error_;
};
} // namespace simphony
