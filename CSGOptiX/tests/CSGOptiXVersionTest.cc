#include <optix.h>
#include <cstdio>

#define xstr(s) str(s)
#define str(s) #s

int main()
{
    const char *vers = xstr(OPTIX_VERSION);

    printf("Using OptiX version %s\n", vers);

    return 0 ; 
}

