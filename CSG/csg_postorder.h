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

#pragma once
/*

1-based indexing of complete binary tree
-------------------------------------------

Exhibits a very regular pattern of the bits::

                                                     depth     elevation

                         1                               0           3   

              10                   11                    1           2   

         100       101        110        111             2           1   
                        
     1000 1001  1010 1011  1100 1101  1110  1111         3           0   
 

This has several advantages:

* child/parent indices can be computed (not stored) so no tree overheads
* postorder traverse can be computed by bit twiddling 

::

    parent(i)         = i/2 
    leftchild(i)      = 2*i + 1 
    rightchild(i)     = 2*i + 2 

    leftmost(height)  =  1 << height 
    postorder_next(i) =  i & 1   ?   i >> 1   :   (i << elevation) + (1 << elevation)  


*/

#ifdef __CUDACC__
#define POSTORDER_DEPTH(currIdx) ( 32 - __clz((currIdx)) - 1 )
#define TREE_HEIGHT(numNodes) ( __ffs((numNodes) + 1) - 2)
#define TREE_DEPTH(nodeIdx) ( 32 - __clz((nodeIdx)) - 1 )
#elif defined(_MSC_VER)
#include <intrin.h>
// ffs/clz semantics from the MSVC intrinsics: ffs is the 1-based index of the
// least significant set bit (0 for none); clz counts leading zeros of a
// nonzero value, matching __builtin_clz whose x==0 case is undefined anyway
inline int csg_postorder_ffs_(int x){ unsigned long i ; return _BitScanForward(&i, static_cast<unsigned long>(x)) ? static_cast<int>(i) + 1 : 0 ; }
inline int csg_postorder_clz_(unsigned x){ unsigned long i ; return _BitScanReverse(&i, x) ? 31 - static_cast<int>(i) : 32 ; }
#define POSTORDER_DEPTH(currIdx) ( 32 - csg_postorder_clz_((currIdx)) - 1 )
#define TREE_HEIGHT(numNodes) ( csg_postorder_ffs_((numNodes) + 1) - 2)
#define TREE_DEPTH(nodeIdx) ( 32 - csg_postorder_clz_((nodeIdx)) - 1 )
#else
#define POSTORDER_DEPTH(currIdx) ( 32 - std::__clz((currIdx)) - 1 )
#define TREE_HEIGHT(numNodes) ( ffs((numNodes) + 1) - 2)
//#define TREE_DEPTH(nodeIdx) ( 32 - std::__clz((nodeIdx)) - 1 )
#define TREE_DEPTH(nodeIdx) ( 32 - __builtin_clz((nodeIdx)) - 1 )
#endif

// see dev/csg/postorder.py 
#define POSTORDER_NEXT(currIdx, elevation )( ((currIdx) & 1) ? (currIdx) >> 1 :  ((currIdx) << (elevation)) + (1 << (elevation)) )

// perfect binary tree assumptions,   2^(h+1) - 1 
#define TREE_NODES(height) ( (0x1 << (1+(height))) - 1 )


