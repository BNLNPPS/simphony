/**
 * @file EventBufferTest.cc
 * @brief Checks photon-slot accounting and ownership transfer at end of event.
 */
#include "EventBuffer.h"

#include <limits>
#include <stdexcept>
#include <type_traits>

namespace
{
void require(bool condition)
{
    if (!condition)
        throw std::runtime_error("unexpected optical event buffer contents");
}
} // namespace

int main()
{
    static_assert(!std::is_copy_constructible_v<simphony::EventBuffer>);
    simphony::EventBuffer torch(7, std::vector<sphoton>{});
    require(torch.photonCount() == 0 && torch.genstepCount() == 0);
    auto& photons = std::get<std::vector<sphoton>>(torch.records);
    photons.resize(3);
    const auto*           data = photons.data();
    simphony::EventBuffer submitted(std::move(torch));
    require(submitted.event_id == 7 && submitted.photonCount() == 3 && submitted.genstepCount() == 1);
    require(std::get<std::vector<sphoton>>(submitted.records).data() == data);

    simphony::EventBuffer charged(8, std::vector<quad6>{});
    require(charged.photonCount() == 0 && charged.genstepCount() == 0);
    auto& steps = std::get<std::vector<quad6>>(charged.records);
    steps.resize(2);
    steps[0].set_numphoton(std::numeric_limits<unsigned>::max());
    steps[1].set_numphoton(1);
    require(charged.photonCount() == static_cast<std::size_t>(std::numeric_limits<unsigned>::max()) + 1);
    require(charged.genstepCount() == 2 && submitted.photonCount() == 3);
}
