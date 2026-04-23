#include <optix.h>
#include <cstdio>

#define xstr(s) str(s)
#define str(s) #s

int main()
{
    const char* vers = xstr(OPTIX_VERSION) ;

    static_assert(OPTIX_VERSION >= 70000, "CSGOptiX requires OptiX 7 or newer.");
    printf("Got supported OptiX version %s\n", vers);

    return 0 ; 
}

