#!/usr/bin/env python
#
# Copyright (c) 2019 Opticks Team. All Rights Reserved.
#
# This file is part of Opticks
# (see https://bitbucket.org/simoncblyth/opticks).
#
# Licensed under the Apache License, Version 2.0 (the "License"); 
# you may not use this file except in compliance with the License.  
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software 
# distributed under the License is distributed on an "AS IS" BASIS, 
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  
# See the License for the specific language governing permissions and 
# limitations under the License.
#

"""
twhite.py: Wavelength Distribution Check
============================================

Creates plot comparing simulated photon wavelength spectrum 
from :doc:`../tests/twhite` against blackbody expectation.

This is checking wavelength sampling against the source distribution used by
torch photon generation.


See Also
----------

* :doc:`source_debug`


"""

import os, sys, logging, numpy as np
log = logging.getLogger(__name__)

import matplotlib.pyplot as plt 
from mpl_toolkits.mplot3d import Axes3D

from opticks.ana.base import opticks_main
from opticks.ana.evt import Evt
from opticks.ana.planck import planck



if __name__ == '__main__':
    plt.ion()
    args = opticks_main(tag="1", det="white", src="torch")

    ## tag = "1"   ## dont have any tag 1 anymore 
    ## tag = "15"     ## so added tag 15,16 to ggv-rainbow with wavelength=0 which is default black body 

    try:
        evt = Evt(tag=args.tag, det=args.det, src=args.src, args=args )
    except IOError as err:
        log.fatal(err)
        sys.exit(args.mrc)


    if not evt.valid:
       log.fatal("failed to load evt %s " % repr(args))
       sys.exit(1) 


    wl = evt.wl
    w0 = evt.recwavelength(0)  

    w = wl
    #w = w0

    wd = np.linspace(60,820,256) - 1.  
    # reduce bin edges by 1nm to avoid aliasing artifact in the histogram

    mid = (wd[:-1]+wd[1:])/2.     # bin middle

    pl = planck(mid, 6500.)
    pl /= pl.sum()

    counts, edges = np.histogram(w, bins=wd )
    fcounts = counts.astype(np.float32)
    fcounts  /= fcounts.sum()


    plt.close()

    plt.plot( edges[:-1], fcounts, drawstyle="steps-mid")

    plt.plot( mid,  pl ) 
    
    plt.axis( [w.min() - 100, w.max() + 100, 0, fcounts.max()*1.1 ]) 

    #plt.hist(w, bins=256)   # 256 is number of unique wavelengths (from record compression)

