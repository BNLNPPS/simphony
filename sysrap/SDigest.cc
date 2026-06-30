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

#include "SDigest.hh"
#include "SSys.hh"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cassert>

#include <iostream>

#include "SLOG.hh"

const plog::Severity SDigest::LEVEL = SLOG::EnvLevel("SDigest", "DEBUG") ;

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

namespace
{
constexpr int MD5_DIGEST_LENGTH_BYTES = 16;
constexpr int MD5_HEX_LENGTH = MD5_DIGEST_LENGTH_BYTES * 2;

/**
 * Format a 16-byte MD5 digest as a 32-character lowercase hex string.
 *
 * The caller must provide at least MD5_HEX_LENGTH + 1 bytes in `out`.
 * Each byte is written with a three-byte snprintf window so fortified
 * libc builds see the true remaining object size at the write location.
 */
void FormatMD5Hex(char* out, const unsigned char* digest)
{
    for (int n = 0; n < MD5_DIGEST_LENGTH_BYTES; ++n)
        std::snprintf(&out[n * 2], 3, "%02x", (unsigned int)digest[n]);

    out[MD5_HEX_LENGTH] = '\0';
}

/**
 * Return a malloc-allocated lowercase hex string for a raw MD5 digest.
 *
 * This preserves the legacy SDigest::finalize/md5digest_ ownership
 * contract: callers that receive this pointer are responsible for free().
 */
char* AllocMD5Hex(const unsigned char* digest)
{
    char* out = (char*)malloc(MD5_HEX_LENGTH + 1);
    FormatMD5Hex(out, digest);
    return out;
}

/**
 * Return a std::string lowercase hex representation of a raw MD5 digest.
 */
std::string StringMD5Hex(const unsigned char* digest)
{
    char buf[MD5_HEX_LENGTH + 1];
    FormatMD5Hex(buf, digest);
    return std::string(buf, buf + MD5_HEX_LENGTH);
}

/**
 * Feed a byte range into an existing MD5 context in fixed-size chunks.
 *
 * Chunking matches the historical implementation and keeps all MD5_Update
 * calls on explicit byte counts instead of relying on null termination.
 */
void UpdateMD5(MD5_CTX& ctx, const char* buffer, int length)
{
    const int blocksize = 512;
    while (length > 0)
    {
        const int chunk = length > blocksize ? blocksize : length;
        MD5_Update(&ctx, buffer, chunk);
        length -= chunk;
        buffer += chunk;
    }
}

/**
 * Finalize an MD5 context and return the digest as a std::string.
 *
 * MD5_Final consumes/finalizes the supplied context, so callers must not
 * update or finalize the same context again after calling this helper.
 */
std::string FinalizeMD5String(MD5_CTX& ctx)
{
    unsigned char digest[MD5_DIGEST_LENGTH_BYTES];
    MD5_Final(digest, &ctx);

    return StringMD5Hex(digest);
}

/**
 * Finalize an MD5 context and return a malloc-allocated hex digest.
 *
 * This consumes/finalizes `ctx` and returns ownership to the caller.
 */
char* FinalizeMD5Hex(MD5_CTX& ctx)
{
    unsigned char digest[MD5_DIGEST_LENGTH_BYTES];
    MD5_Final(digest, &ctx);

    return AllocMD5Hex(digest);
}

/**
 * Compute the MD5 digest of a byte range and return it as a std::string.
 */
std::string StringMD5Digest(const char* buffer, int length)
{
    MD5_CTX ctx;
    MD5_Init(&ctx);
    UpdateMD5(ctx, buffer, length);

    return FinalizeMD5String(ctx);
}

/**
 * Compute the MD5 digest of a byte range as a malloc-allocated hex string.
 *
 * Used only by legacy APIs that return raw pointers; callers must free()
 * the returned buffer.
 */
char* AllocMD5Digest(const char* buffer, int length)
{
    MD5_CTX ctx;
    MD5_Init(&ctx);
    UpdateMD5(ctx, buffer, length);

    return FinalizeMD5Hex(ctx);
}
} // namespace

#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif

/**


Byte range covering multiple bufsize::

           ---------- 
      i0   ~~~~~~~~~~
           ----------  
           
           ----------  
      i1   ~~~~~~~~~~
           ----------  
      
Byte range within one bufsize::


           ---------- 
      i0   ~~~~~~~~~~
           
      i1   ~~~~~~~~~~
           ----------  
 


           ----------  

**/

std::string SDigest::DigestPathInByteRange(const char* path, int i0, int i1, unsigned bufsize)
{
    LOG(LEVEL) 
        << " path " << path 
        << " i0 " << i0 
        << " i1 " << i1 
        << " bufsize " << bufsize
        ; 

    FILE* fp = fopen(path, "rb");
    if (fp == NULL) 
    {
        LOG(error) << "failed to open path [" << path << "]"  ; 
        return "" ; 
    }
    SDigest dig ; 
    char* data = new char[bufsize] ; 
    int bytes ; 

    int beg = 0 ; // byte index of beginning of buffer in the file
    int end = 0 ; 
    int tot = 0 ;  

    assert( i1 > i0 ) ; 

    while ((bytes = fread (data, 1, bufsize, fp)) != 0) 
    {
        end = beg + bytes ;  

        bool starts = i0 >= beg && i0 < end ;  // capture starts within this bufsize full  
        bool ends   = i1 <= end ;              // capture ends within this bufsize full 

        int idx0(-1) ;  
        int idx1(-1) ;  

        if( starts && ends )
        {
            idx0 = i0 - beg ;   
            idx1 = i1 - beg ;   
        }
        else if( starts && !ends )
        {
            idx0 = i0 - beg ;   
            idx1 = bytes ;   
        }
        else if( !starts && ends )
        { 
            idx0 = 0 ; 
            idx1 = i1 - beg ; 
        }
        else if( !starts && !ends && tot > 0 )  // entire buffer goes to update
        {
            idx0 = 0 ; 
            idx1 = bytes ;   
        } 


        int nup =  idx1 - idx0 ; 
        bool update = idx0 > -1 && idx1 > -1 ; 

        std::string x = update ? SSys::xxd( data+idx0, nup ) : "-" ; 

        LOG(LEVEL)
           << " bytes " << std::setw(8) << bytes 
           << " beg "  << std::setw(8) << beg
           << " end "  << std::setw(8) << end
           << " idx0 " << std::setw(8) << idx0 
           << " idx1 " << std::setw(8) << idx1
           << " nup "  << std::setw(8) << nup
           << ( starts ? " S " : "   ")
           << ( ends ?   " E " : "   ")
           << ( update ? " U " : "   ")
           << " x " << x  
           ; 

        if(update)
        {
            dig.update(data+idx0, nup );   
            tot += nup ;  
        } 

        if( ends ) break ; 

        beg += bytes ;   
    }

    delete[] data ;

    std::string sdig = FinalizeMD5String(dig.m_ctx);
    LOG(LEVEL) << " sdig " << sdig ; 
    return sdig ; 
}



std::string SDigest::DigestPath(const char* path, unsigned bufsize)
{
    FILE* fp = fopen(path, "rb");
    if (fp == NULL) 
    {
        std::cerr << "failed to open path [" << path << "]" <<  std::endl ; 
        return "" ; 
    }
    SDigest dig ; 
    char* data = new char[bufsize] ; 
    int bytes ; 
    while ((bytes = fread (data, 1, bufsize, fp)) != 0) dig.update(data, bytes);   
    // NB must update just with the bytes read, not the bufsize
    delete[] data ;
    return FinalizeMD5String(dig.m_ctx);
}




#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
#endif

// https://stackoverflow.com/questions/10324611/how-to-calculate-the-md5-hash-of-a-large-file-in-c


std::string SDigest::DigestPath2(const char* path)
{
    FILE* fp = fopen (path, "rb");
    MD5_CTX mdContext;
    int bytes;
    char data[8192];

    if (fp == NULL) {
        printf ("%s can't be opened.\n", path);
        return "";
    }

    MD5_Init (&mdContext);
    while ((bytes = fread (data, 1, 8192, fp)) != 0)
        MD5_Update (&mdContext, data, bytes);

    fclose (fp);

    return FinalizeMD5String(mdContext);
}




std::string SDigest::Buffer(const char *buffer, int length) 
{
    return StringMD5Digest(buffer, length);
}


SDigest::SDigest()
{
   MD5_Init(&m_ctx); 
}


#if defined(__GNUC__) || defined(__clang__)
#pragma GCC diagnostic pop
#endif







const char* SDigest::hexchar = "0123456789abcdef" ;  

bool SDigest::IsDigest(const char* s)
{
    if( s == NULL ) return false ; 
    if( strlen(s) != 32 ) return false ;  
    for(int i=0 ; i < 32 ; i++ )
    {
        char c = *(s + i) ; 
        if(strchr(hexchar,c) == NULL) return false  ;
    }
    return true  ; 
}






SDigest::~SDigest()
{
}

void SDigest::update(const std::string& str)
{
    update( (char*)str.c_str(), strlen(str.c_str()) );
}

void SDigest::update(char* buffer, int length)
{
    UpdateMD5(m_ctx, buffer, length);
}

void SDigest::update_str(const char* str )
{
    UpdateMD5(m_ctx, str, strlen(str));
}


char* SDigest::finalize()
{
    return FinalizeMD5Hex(m_ctx);
}



std::string SDigest::md5digest( const char* buffer, int len )
{
    return StringMD5Digest(buffer, len);
}

const char* SDigest::md5digest_( const char* buffer, int len )
{
    return AllocMD5Digest(buffer, len);
}


std::string SDigest::digest( void* buffer, int len )
{
    SDigest dig ; 
    dig.update( (char*)buffer, len );
    return FinalizeMD5String(dig.m_ctx);
}

std::string SDigest::digest( std::vector<std::string>& ss)
{
    SDigest dig ; 
    for(unsigned i=0 ; i < ss.size() ; i++) dig.update( ss[i] ) ;
    return FinalizeMD5String(dig.m_ctx);
}


std::string SDigest::digest_skipdupe( std::vector<std::string>& ss)
{
    SDigest dig ; 
    for(unsigned i=0 ; i < ss.size() ; i++) 
    {
        if( i > 0 && ss[i].compare(ss[i-1].c_str()) == 0 ) continue ;    
        dig.update( ss[i] ) ;
    }
    return FinalizeMD5String(dig.m_ctx);
}
