/**

~/o/sysrap/tests/srng_test.sh 

**/

#include <iostream>
#include "srng.h"

int main()
{
    std::cout << "srng_Desc<XORWOW>()\n" << srng_Desc<XORWOW>() << "\n\n" ; 
    std::cout << "srng_Desc<Philox>()\n" << srng_Desc<Philox>() << "\n\n" ; 
    std::cout << "srng_Desc<RNG>()\n" << srng_Desc<RNG>() << "\n\n" ; 


    std::cout << "srng<RNG>::NAME :[" << srng<RNG>::NAME << "]\n" ; 

    std::cout << " srng_IsXORWOW<RNG>()" << srng_IsXORWOW<RNG>() << "\n" ; 
    std::cout << " srng_IsPhilox<RNG>()" << srng_IsPhilox<RNG>() << "\n" ; 
 
    std::cout << " srng_Matches<RNG>(\"Cheese\")" << srng_Matches<RNG>("Cheese") << "\n" ; 
    std::cout << " srng_Matches<RNG>(\"Cheese XORWOW \")" << srng_Matches<RNG>("Cheese XORWOW") << "\n" ; 
    std::cout << " srng_Matches<RNG>(\"Cheese Philox \")" << srng_Matches<RNG>("Cheese Philox") << "\n" ; 

    return 0 ;  
}
