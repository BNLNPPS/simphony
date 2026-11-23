#pragma once

#include <cstddef>
#include <utility>
#include <variant>
#include <vector>

#include "scuda.h"
#include "squad.h"

#include "sphoton.h"

namespace simphony
{
/** Optical work owned by one Geant4 event until end-of-event submission. */
struct EventBuffer
{
    using Records = std::variant<std::vector<sphoton>, std::vector<quad6>>;

    EventBuffer(int event, Records data) :
        event_id(event),
        records(std::move(data))
    {
    }
    EventBuffer(EventBuffer&&) = default;
    EventBuffer& operator=(EventBuffer&&) = default;

    std::size_t photonCount() const
    {
        if (const auto* photons = std::get_if<std::vector<sphoton>>(&records))
            return photons->size();

        std::size_t count = 0;
        for (const auto& step : std::get<std::vector<quad6>>(records))
            count += step.numphoton();
        return count;
    }

    std::size_t genstepCount() const
    {
        if (const auto* steps = std::get_if<std::vector<quad6>>(&records))
            return steps->size();
        // The transport represents a nonempty array of input photons as one genstep.
        return std::get<std::vector<sphoton>>(records).empty() ? 0 : 1;
    }

    int     event_id;
    Records records;
};
} // namespace simphony
