#include "ssys.h"
#include "sframe.h"

struct sframe_test
{
    static int main();
    static int MakeFromAxis();
    static int roundtrip();
};

int sframe_test::main()
{
    //const char* test = "ALL" ;
    //const char* test = "MakeFromAxis" ;
    const char* test = "roundtrip" ;
    const char* TEST = ssys::getenvvar("TEST", test);
    bool ALL = strcmp(TEST, "ALL") == 0 ;
    int rc = 0 ;
    if(ALL||0==strcmp(TEST,"roundtrip")) rc += roundtrip();
    if(ALL||0==strcmp(TEST,"MakeFromAxis")) rc += MakeFromAxis();
    return rc ;
}

int sframe_test::MakeFromAxis()
{
    const char* tpde = "45,45,0,1000" ;
    sframe mf = sframe::MakeFromAxis<double>(tpde, ',');

    std::cout
         << "sframe_test::MakeFromAxis"
         << " tpde " << tpde
         << "\n"
         << mf.desc()
         << "\n"
         ;
    return 0 ;
}

int sframe_test::roundtrip()
{
    sframe a ;
    a.set_name("sframe_test::roundtrip");

    a.aux0.x = 1 ;
    a.aux0.y = 2 ;
    a.aux0.z = 3 ;
    a.aux0.w = 4 ;

    a.aux1.x = 10 ;
    a.aux1.y = 20 ;
    a.aux1.z = 30 ;
    a.aux1.w = 40 ;

    a.aux2.x = 100 ;
    a.aux2.y = 200 ;
    a.aux2.z = 300 ;
    a.aux2.w = 400 ;

    std::array<double,6> bb = {-300., -200., -100., +100., +200., +300. } ;
    a.set_bb(bb.data());

    std::cout << "A\n" << a.desc() << std::endl;
    a.save("$FOLD");

    sframe b = sframe::Load("$FOLD");
    std::cout << "B\n" << b.desc() << std::endl;

    return 0;
}

int main()
{
    return sframe_test::main();
}
