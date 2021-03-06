# Copyright (c) 2006-2007 The Regents of The University of Michigan
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met: redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer;
# redistributions in binary form must reproduce the above copyright
# notice, this list of conditions and the following disclaimer in the
# documentation and/or other materials provided with the distribution;
# neither the name of the copyright holders nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
# Authors: Steve Reinhardt

import m5
from m5.objects import *
m5.util.addToPath('../configs/common')
import FSConfig


# --------------------
# Base L1 Cache
# ====================

class L1(BaseCache):
    latency = '1ns'
    block_size = 64
    mshrs = 4
    tgts_per_mshr = 20
    is_top_level = True

# ----------------------
# Base L2 Cache
# ----------------------

class L2(BaseCache):
    block_size = 64
    latency = '10ns'
    mshrs = 92
    tgts_per_mshr = 16
    write_buffers = 8

# ---------------------
# I/O Cache
# ---------------------
class IOCache(BaseCache):
    assoc = 8
    block_size = 64
    latency = '50ns'
    mshrs = 20
    size = '1kB'
    tgts_per_mshr = 12
    addr_range=AddrRange(0, size='8GB')
    forward_snoops = False
    is_top_level = True

#cpu
cpus = [ DerivO3CPU(cpu_id=i) for i in xrange(2) ]
#the system
system = FSConfig.makeLinuxAlphaSystem('timing')

system.cpu = cpus
#create the l1/l2 bus
system.toL2Bus = Bus()
system.bridge.filter_ranges_a=[AddrRange(0, Addr.max)]
system.bridge.filter_ranges_b=[AddrRange(0, size='8GB')]
system.iocache = IOCache()
system.iocache.cpu_side = system.iobus.port
system.iocache.mem_side = system.membus.port


#connect up the l2 cache
system.l2c = L2(size='4MB', assoc=8)
system.l2c.cpu_side = system.toL2Bus.port
system.l2c.mem_side = system.membus.port
system.l2c.num_cpus = 2

#connect up the cpu and l1s
for c in cpus:
    c.addPrivateSplitL1Caches(L1(size = '32kB', assoc = 1),
                                L1(size = '32kB', assoc = 4))
    # connect cpu level-1 caches to shared level-2 cache
    c.connectAllPorts(system.toL2Bus, system.membus)
    c.clock = '2GHz'

root = Root(system=system)
m5.ticks.setGlobalFrequency('1THz')

