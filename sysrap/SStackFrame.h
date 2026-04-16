#pragma once

/**
SStackFrame
=============

Used for stack frame introspection based on *cxxabi.h*

**/

#include "SYSRAP_API_EXPORT.hh"
#include <ostream>

struct SYSRAP_API SStackFrame
{
    static char *TrimArgs(const char *signature);

    SStackFrame(char *line);
    ~SStackFrame();

    void parse();
    char *demangle(); // fails for non C++ symbols
    void dump();
    void dump(std::ostream &out);

    char *line;
    char *name;
    char *offset;
    char *end_offset;

    char *func; // only func and smry are "owned"
    char *smry;
};

#include <cxxabi.h>
#include <iomanip>
#include <iostream>

inline SStackFrame::SStackFrame(char *line_)
    : line(line_), name(nullptr), offset(nullptr), end_offset(nullptr), func(nullptr), smry(nullptr)
{
    parse();
    if (func)
        smry = TrimArgs(func);
}

inline SStackFrame::~SStackFrame()
{
    free(func);
    free(smry);
}

inline void SStackFrame::dump()
{
    std::ostream &out = std::cout;
    dump(out);
}

/**
SStackFrame::TrimArgs
-----------------------

G4VEmProcess::PostStepGetPhysicalInteractionLength(G4Track const&, double, G4ForceCondition*)
->
G4VEmProcess::PostStepGetPhysicalInteractionLength

**/

inline char *SStackFrame::TrimArgs(const char *signature)
{
    char *smry = strdup(signature);
    for (char *p = smry; *p; ++p)
        if (*p == '(')
            *p = '\0';
    return smry;
}

inline void SStackFrame::parse()
{
    /**
    /home/blyth/local/opticks/externals/lib64/libG4tracking.so(_ZN10G4VProcess12PostStepGPILERK7G4TrackdP16G4ForceCondition+0x42)
    [0x7ffff36ff9b2]
    **/
    for (char *p = line; *p; ++p)
    {
        if (*p == '(')
            name = p;
        else if (*p == '+')
            offset = p;
        else if (*p == ')' && (offset || name))
            end_offset = p;
    }

    if (name && end_offset && (name < end_offset))
    {
        *name++ = '\0';       // plant terminator into line
        *end_offset++ = '\0'; // plant terminator into name
        if (offset)
            *offset++ = '\0';
        func = demangle();
    }
}
inline void SStackFrame::dump(std::ostream &out)
{
    if (func)
    {
        out << std::setw(25) << std::left << (end_offset ? end_offset : "") // addr
            << " " << std::setw(10) << std::left << offset << " " << std::setw(60) << line << " " << std::setw(60)
            << std::left << func << std::endl;
    }
    else
    {
        out << line << std::endl;
    }
}

inline char *SStackFrame::demangle() // demangling fails for non C++ symbols
{
    int status;
    char *ret = abi::__cxa_demangle(name, NULL, NULL, &status);
    return status == 0 ? ret : NULL;
}
