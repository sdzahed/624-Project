/*
 * Copyright (c) 2003-2009 The Regents of The University of Michigan
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met: redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer;
 * redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution;
 * neither the name of the copyright holders nor the names of its
 * contributors may be used to endorse or promote products derived from
 * this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * Authors: Steve Reinhardt
 *          Gabe Black
 *          Kevin Lim
 */

#include "arch/alpha/regredir.hh"
#include "config/full_system.hh"

namespace AlphaISA {

#if FULL_SYSTEM
const int reg_redir[NumIntRegs] = {
    /*  0 */ 0, 1, 2, 3, 4, 5, 6, 7,
    /*  8 */ 32, 33, 34, 35, 36, 37, 38, 15,
    /* 16 */ 16, 17, 18, 19, 20, 21, 22, 23,
    /* 24 */ 24, 39, 26, 27, 28, 29, 30, 31 };
#else
const int reg_redir[NumIntRegs] = {
    /*  0 */ 0, 1, 2, 3, 4, 5, 6, 7,
    /*  8 */ 8, 9, 10, 11, 12, 13, 14, 15,
    /* 16 */ 16, 17, 18, 19, 20, 21, 22, 23,
    /* 24 */ 24, 25, 26, 27, 28, 29, 30, 31 };
#endif

} // namespace AlphaISA
