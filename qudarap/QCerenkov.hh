#pragma once

// Host owner of the GPU Cerenkov context and its boundary/property resources.
#include <string>
#include "QUDARAP_API_EXPORT.hh"

struct qcerenkov;

struct QUDARAP_API QCerenkov
{
    static const QCerenkov* INSTANCE;
    static const QCerenkov* Get();
    static qcerenkov* MakeInstance();

    qcerenkov* cerenkov;
    qcerenkov* d_cerenkov;

    QCerenkov();
    std::string desc() const;
};
