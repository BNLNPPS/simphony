#include <cassert>
#include <sstream>

#include "QBase.hh"
#include "QBnd.hh"
#include "QCerenkov.hh"
#include "QProp.hh"
#include "QU.hh"
#include "qcerenkov.h"
#include "scuda.h"
#include "squad.h"

const QCerenkov* QCerenkov::INSTANCE = nullptr;
const QCerenkov* QCerenkov::Get()
{
    return INSTANCE;
}

qcerenkov* QCerenkov::MakeInstance() // static
{
    const QBase* base = QBase::Get();
    assert(base);

    const QBnd* bnd = QBnd::Get();
    assert(bnd);

    const QProp<float>* prop = QProp<float>::Get();
    // assert(prop);

    qcerenkov* ck = new qcerenkov;
    ck->base = base->d_base;
    ck->bnd = bnd->d_qb;
    ck->prop = prop ? prop->d_prop : nullptr;
    return ck;
}

QCerenkov::QCerenkov() :
    cerenkov(MakeInstance()),
    d_cerenkov(QU::UploadArray<qcerenkov>(cerenkov, 1, "QCerenkov::QCerenkov/d_cerenkov.0"))
{
    INSTANCE = this;
}

std::string QCerenkov::desc() const
{
    std::stringstream ss;
    ss << "QCerenkov"
       << " cerenkov " << cerenkov
       << " d_cerenkov " << d_cerenkov;
    return ss.str();
}
