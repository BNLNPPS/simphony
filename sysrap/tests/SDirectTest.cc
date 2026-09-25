/*
 * Copyright (c) 2019 Opticks Team. All Rights Reserved.
 *
 * This file is part of Opticks
 * (see https://bitbucket.org/simoncblyth/opticks).
 *
 * Licensed under the Apache License, Version 2.0 (the "License"); 
 * you may not use this file except in compliance with the License.  
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software 
 * distributed under the License is distributed on an "AS IS" BASIS, 
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  
 * See the License for the specific language governing permissions and 
 * limitations under the License.
 */

#include "sdirect.h"
#include <cassert>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>

#include "SSys.hh"
#include "OPTICKS_LOG.hh"


void test_cout_cerr_redirect(const char* msg)
{
    std::stringstream coutbuf;
    std::stringstream cerrbuf;
    std::streambuf*   original_cout = std::cout.rdbuf();
    std::streambuf*   original_cerr = std::cerr.rdbuf();
    {
        sdirect::cout_ out_(coutbuf.rdbuf());
        sdirect::cerr_ err_(cerrbuf.rdbuf());

        std::cout << "captured stdout\n";
        std::cerr << "captured stderr\n";

        // dtors of the redirect structs reset back to standard cout/cerr streams
    }

    std::string out = coutbuf.str();
    std::string err = cerrbuf.str();
    assert(out == "captured stdout\n");
    assert(err == "captured stderr\n");
    assert(std::cout.rdbuf() == original_cout);
    assert(std::cerr.rdbuf() == original_cerr);

    LOG(info) << " captured cout " << out.size()  ; 
    std::cout << "[" << std::endl << out << "]" << std::endl  ; 

    LOG(info) << " captured cerr " << err.size() ; 
    std::cout << "[" << std::endl << err << "]" << std::endl  ; 

    SSys::Dump(msg); 
}


void method_expecting_to_write_to_file( std::ofstream& fp, std::vector<std::string>& msgv )
{
    for(unsigned i=0 ; i < msgv.size() ; i++ )
    {
        const char* pt = msgv[i].c_str() ;
        fp.write(pt, msgv[i].size());
    }
} 

void test_stream_redirect()
{
    std::ofstream     fp("/dev/null", std::ios::out);
    std::stringstream ss;

    std::ostream&   stream = fp;
    std::streambuf* original = stream.rdbuf();

    std::vector<std::string> msgv;
    msgv.push_back("hello");
    msgv.push_back("world");

    {
        sdirect::ostream_ rdir(ss, fp);
        method_expecting_to_write_to_file(fp, msgv);
    }
    assert(ss.str() == "helloworld");
    assert(stream.rdbuf() == original);

    std::cout <<  ss.str() << std::endl ; 
}


int main(int argc, char** argv)
{
    OPTICKS_LOG(argc, argv);

    LOG(info) << argv[0];

    SSys::Dump(argv[0]);

    test_cout_cerr_redirect(argv[0]);
    test_stream_redirect(); 


    return 0 ; 
}

