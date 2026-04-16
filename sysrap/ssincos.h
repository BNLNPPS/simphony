#pragma once

#include <cmath>

template <typename T> void ssincos(const T angle, T &s, T &c)
{
    s = std::sin(angle);
    c = std::cos(angle);
}
