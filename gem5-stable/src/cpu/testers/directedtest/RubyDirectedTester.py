# Copyright (c) 2010 Advanced Micro Devices, Inc.
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
# Authors: Brad Beckmann

from m5.SimObject import SimObject
from MemObject import MemObject
from m5.params import *
from m5.proxy import *

class DirectedGenerator(SimObject):
    type = 'DirectedGenerator'
    abstract = True
    num_cpus = Param.Int("num of cpus")

class SeriesRequestGenerator(DirectedGenerator):
    type = 'SeriesRequestGenerator'
    addr_increment_size = Param.Int(64, "address increment size")
    issue_writes = Param.Bool(True, "issue writes if true, otherwise reads")

class InvalidateGenerator(DirectedGenerator):
    type = 'InvalidateGenerator'
    addr_increment_size = Param.Int(64, "address increment size")

class RubyDirectedTester(MemObject):
    type = 'RubyDirectedTester'
    cpuPort = VectorPort("the cpu ports")
    requests_to_complete = Param.Int("checks to complete")
    generator = Param.DirectedGenerator("the request generator")
