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

#if defined(_MSC_VER)
#  if defined(SIMPHONY_SHARED_LIBS)
#    if defined(CSGOptiX_EXPORTS)
#      define CSGOPTIX_API __declspec(dllexport)
#    else
#      define CSGOPTIX_API __declspec(dllimport)
#    endif
#  else
#    define CSGOPTIX_API
#  endif
#else
#  define CSGOPTIX_API __attribute__((visibility("default")))
#endif
