#pragma once

#include <math.h>


template<typename T>
void ssincos(const T angle, T& s, T& c)
{
    sincos( angle, &s, &c);
}
