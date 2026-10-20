#include "s_csg.h"
s_csg* s_csg::INSTANCE = nullptr;

// Storage for the CSG node, parameter, bounding-box, and transform pools.
sn::POOL* sn::pool = nullptr;

s_pa::POOL* s_pa::pool = nullptr;

s_bb::POOL* s_bb::pool = nullptr;

s_tv::POOL* s_tv::pool = nullptr;
