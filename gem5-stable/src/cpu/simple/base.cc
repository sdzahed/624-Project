/*
 * Copyright (c) 2010 ARM Limited
 * All rights reserved
 *
 * The license below extends only to copyright in the software and shall
 * not be construed as granting a license to any other intellectual
 * property including but not limited to intellectual property relating
 * to a hardware implementation of the functionality of the software
 * licensed hereunder.  You may use the software subject to the license
 * terms below provided that you ensure that this notice is replicated
 * unmodified and in its entirety in all distributions of the software,
 * modified or unmodified, in source code or in binary form.
 *
 * Copyright (c) 2002-2005 The Regents of The University of Michigan
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
 */

#include "arch/faults.hh"
#include "arch/utility.hh"
#include "base/loader/symtab.hh"
#include "base/cp_annotate.hh"
#include "base/cprintf.hh"
#include "base/inifile.hh"
#include "base/misc.hh"
#include "base/pollevent.hh"
#include "base/range.hh"
#include "base/trace.hh"
#include "base/types.hh"
#include "config/the_isa.hh"
#include "cpu/simple/base.hh"
#include "cpu/base.hh"
#include "cpu/exetrace.hh"
#include "cpu/profile.hh"
#include "cpu/simple_thread.hh"
#include "cpu/smt.hh"
#include "cpu/static_inst.hh"
#include "cpu/thread_context.hh"
#include "debug/Decode.hh"
#include "debug/Fetch.hh"
#include "debug/Quiesce.hh"
#include "mem/packet.hh"
#include "mem/request.hh"
#include "params/BaseSimpleCPU.hh"
#include "sim/byteswap.hh"
#include "sim/debug.hh"
#include "sim/sim_events.hh"
#include "sim/sim_object.hh"
#include "sim/stats.hh"
#include "sim/system.hh"

#if FULL_SYSTEM
#include "arch/kernel_stats.hh"
#include "arch/stacktrace.hh"
#include "arch/tlb.hh"
#include "arch/vtophys.hh"
#else // !FULL_SYSTEM
#include "mem/mem_object.hh"
#endif // FULL_SYSTEM

#include "debug/Zahed.hh"
#include "debug/Zahed1.hh"
using namespace std;
using namespace TheISA;

// to store/restore state
std::map<CR3, Trapframe::TrapFrame *> tfs;

BaseSimpleCPU::BaseSimpleCPU(BaseSimpleCPUParams *p)
    : BaseCPU(p), traceData(NULL), thread(NULL), predecoder(NULL)
{
#if FULL_SYSTEM
    thread = new SimpleThread(this, 0, p->system, p->itb, p->dtb);
#else
    thread = new SimpleThread(this, /* thread_num */ 0, p->workload[0],
            p->itb, p->dtb);
#endif // !FULL_SYSTEM

    thread->setStatus(ThreadContext::Halted);

    tc = thread->getTC();

    numInst = 0;
    startNumInst = 0;
    numLoad = 0;
    startNumLoad = 0;
    lastIcacheStall = 0;
    lastDcacheStall = 0;

    threadContexts.push_back(tc);


    fetchOffset = 0;
    stayAtPC = false;
}

BaseSimpleCPU::~BaseSimpleCPU()
{
}

void
BaseSimpleCPU::deallocateContext(int thread_num)
{
    // for now, these are equivalent
    suspendContext(thread_num);
}


void
BaseSimpleCPU::haltContext(int thread_num)
{
    // for now, these are equivalent
    suspendContext(thread_num);
}


void
BaseSimpleCPU::regStats()
{
    using namespace Stats;

    BaseCPU::regStats();

    numInsts
        .name(name() + ".num_insts")
        .desc("Number of instructions executed")
        ;

    numIntAluAccesses
        .name(name() + ".num_int_alu_accesses")
        .desc("Number of integer alu accesses")
        ;

    numFpAluAccesses
        .name(name() + ".num_fp_alu_accesses")
        .desc("Number of float alu accesses")
        ;

    numCallsReturns
        .name(name() + ".num_func_calls")
        .desc("number of times a function call or return occured")
        ;

    numCondCtrlInsts
        .name(name() + ".num_conditional_control_insts")
        .desc("number of instructions that are conditional controls")
        ;

    numIntInsts
        .name(name() + ".num_int_insts")
        .desc("number of integer instructions")
        ;

    numFpInsts
        .name(name() + ".num_fp_insts")
        .desc("number of float instructions")
        ;

    numIntRegReads
        .name(name() + ".num_int_register_reads")
        .desc("number of times the integer registers were read")
        ;

    numIntRegWrites
        .name(name() + ".num_int_register_writes")
        .desc("number of times the integer registers were written")
        ;

    numFpRegReads
        .name(name() + ".num_fp_register_reads")
        .desc("number of times the floating registers were read")
        ;

    numFpRegWrites
        .name(name() + ".num_fp_register_writes")
        .desc("number of times the floating registers were written")
        ;

    numMemRefs
        .name(name()+".num_mem_refs")
        .desc("number of memory refs")
        ;

    numStoreInsts
        .name(name() + ".num_store_insts")
        .desc("Number of store instructions")
        ;

    numLoadInsts
        .name(name() + ".num_load_insts")
        .desc("Number of load instructions")
        ;

    notIdleFraction
        .name(name() + ".not_idle_fraction")
        .desc("Percentage of non-idle cycles")
        ;

    idleFraction
        .name(name() + ".idle_fraction")
        .desc("Percentage of idle cycles")
        ;

    numBusyCycles
        .name(name() + ".num_busy_cycles")
        .desc("Number of busy cycles")
        ;

    numIdleCycles
        .name(name()+".num_idle_cycles")
        .desc("Number of idle cycles")
        ;

    icacheStallCycles
        .name(name() + ".icache_stall_cycles")
        .desc("ICache total stall cycles")
        .prereq(icacheStallCycles)
        ;

    dcacheStallCycles
        .name(name() + ".dcache_stall_cycles")
        .desc("DCache total stall cycles")
        .prereq(dcacheStallCycles)
        ;

    icacheRetryCycles
        .name(name() + ".icache_retry_cycles")
        .desc("ICache total retry cycles")
        .prereq(icacheRetryCycles)
        ;

    dcacheRetryCycles
        .name(name() + ".dcache_retry_cycles")
        .desc("DCache total retry cycles")
        .prereq(dcacheRetryCycles)
        ;

    idleFraction = constant(1.0) - notIdleFraction;
    numIdleCycles = idleFraction * numCycles;
    numBusyCycles = (notIdleFraction)*numCycles;
}

void
BaseSimpleCPU::resetStats()
{
//    startNumInst = numInst;
     notIdleFraction = (_status != Idle);
}

void
BaseSimpleCPU::serialize(ostream &os)
{
    SERIALIZE_ENUM(_status);
    BaseCPU::serialize(os);
//    SERIALIZE_SCALAR(inst);
    nameOut(os, csprintf("%s.xc.0", name()));
    thread->serialize(os);
}

void
BaseSimpleCPU::unserialize(Checkpoint *cp, const string &section)
{
    UNSERIALIZE_ENUM(_status);
    BaseCPU::unserialize(cp, section);
//    UNSERIALIZE_SCALAR(inst);
    thread->unserialize(cp, csprintf("%s.xc.0", section));
}

void
change_thread_state(ThreadID tid, int activate, int priority)
{
}

#if FULL_SYSTEM
Addr
BaseSimpleCPU::dbg_vtophys(Addr addr)
{
    return vtophys(tc, addr);
}
#endif // FULL_SYSTEM

#if FULL_SYSTEM
void
BaseSimpleCPU::wakeup()
{
    if (thread->status() != ThreadContext::Suspended)
        return;

    DPRINTF(Quiesce,"Suspended Processor awoke\n");
    thread->activate();
}
#endif // FULL_SYSTEM

void
BaseSimpleCPU::checkForInterrupts()
{
#if FULL_SYSTEM
    if (checkInterrupts(tc)) {
        Fault interrupt = interrupts->getInterrupt(tc);

        if (interrupt != NoFault) {
            fetchOffset = 0;
            interrupts->updateIntrInfo(tc);

            if (TheISA::inUserMode(tc)){
                Trapframe::TrapFrame *tf = new Trapframe::TrapFrame();
                tf->cr3 = tc->readMiscRegNoEffect(MISCREG_CR3);
                tf->pcState = tc->pcState();
                tf->intRegs[INTREG_EAX] = tc->readIntReg(INTREG_EAX);
                tf->intRegs[INTREG_EDX] = tc->readIntReg(INTREG_EDX);
                DPRINTF(Zahed, "[0x%08x] Storing state on %s for CR3 [0x%08x] with npc [0x%08x] and edx [0x%08x]\n", tc, interrupt->name(), tf->cr3, tf->pcState.npc(), tf->intRegs[INTREG_EDX]);
                tfs[tf->cr3] = tf;
            }

            interrupt->invoke(tc);
            predecoder.reset();
        }
    }
#endif
}


void
BaseSimpleCPU::setupFetchRequest(Request *req)
{
    Addr instAddr = thread->instAddr();

    // set up memory request for instruction fetch
    DPRINTF(Fetch, "Fetch: PC:%08p\n", instAddr);

    Addr fetchPC = (instAddr & PCMask) + fetchOffset;
    req->setVirt(0, fetchPC, sizeof(MachInst), Request::INST_FETCH, instAddr);
}


void
BaseSimpleCPU::preExecute()
{
    // maintain $r0 semantics
    thread->setIntReg(ZeroReg, 0);
#if THE_ISA == ALPHA_ISA
    thread->setFloatReg(ZeroReg, 0.0);
#endif // ALPHA_ISA

    // check for instruction-count-based events
    comInstEventQueue[0]->serviceEvents(numInst);
    system->instEventQueue.serviceEvents(system->totalNumInsts);

    // decode the instruction
    inst = gtoh(inst);

    TheISA::PCState pcState = thread->pcState();

    if (isRomMicroPC(pcState.microPC())) {
        stayAtPC = false;
        curStaticInst = microcodeRom.fetchMicroop(pcState.microPC(),
                                                  curMacroStaticInst);
    } else if (!curMacroStaticInst) {
        //We're not in the middle of a macro instruction
        StaticInstPtr instPtr = NULL;

        //Predecode, ie bundle up an ExtMachInst
        //This should go away once the constructor can be set up properly
        predecoder.setTC(thread->getTC());
        //If more fetch data is needed, pass it in.
        Addr fetchPC = (pcState.instAddr() & PCMask) + fetchOffset;
        //if(predecoder.needMoreBytes())
            predecoder.moreBytes(pcState, fetchPC, inst);
        //else
        //    predecoder.process();

        //If an instruction is ready, decode it. Otherwise, we'll have to
        //fetch beyond the MachInst at the current pc.
        if (predecoder.extMachInstReady()) {
            stayAtPC = false;
            ExtMachInst machInst = predecoder.getExtMachInst(pcState);
            thread->pcState(pcState);
            instPtr = thread->decoder.decode(machInst, pcState.instAddr());
        } else {
            stayAtPC = true;
            fetchOffset += sizeof(MachInst);
        }

        //If we decoded an instruction and it's microcoded, start pulling
        //out micro ops
        if (instPtr && instPtr->isMacroop()) {
            curMacroStaticInst = instPtr;
            curStaticInst = curMacroStaticInst->fetchMicroop(pcState.microPC());
        } else {
            curStaticInst = instPtr;
        }
    } else {
        //Read the next micro op from the macro op
        curStaticInst = curMacroStaticInst->fetchMicroop(pcState.microPC());
    }

    //If we decoded an instruction this "tick", record information about it.
    if(curStaticInst)
    {
#if TRACING_ON
        traceData = tracer->getInstRecord(curTick(), tc,
                curStaticInst, thread->pcState(), curMacroStaticInst);

        DPRINTF(Decode,"Decode: Decoded %s instruction: 0x%x\n",
                curStaticInst->getName(), curStaticInst->machInst);
#endif // TRACING_ON
    }
}

bool
BaseSimpleCPU::postExecute()
{
    assert(curStaticInst);
    bool restored = false;

    TheISA::PCState pc = tc->pcState();
    Addr instAddr = pc.instAddr();
#if FULL_SYSTEM
    if (thread->profile) {
        bool usermode = TheISA::inUserMode(tc);
        thread->profilePC = usermode ? 1 : instAddr;
        ProfileNode *node = thread->profile->consume(tc, curStaticInst);
        if (node)
            thread->profileNode = node;
    }
#endif

    if (curStaticInst->isMemRef()) {
        numMemRefs++;
    }

    if (curStaticInst->isLoad()) {
        ++numLoad;
        comLoadEventQueue[0]->serviceEvents(numLoad);
    }

    if (CPA::available()) {
        CPA::cpa()->swAutoBegin(tc, pc.nextInstAddr());
    }

    /* Power model statistics */
    //integer alu accesses
    if (curStaticInst->isInteger()){
        numIntAluAccesses++;
        numIntInsts++;
    }

    //float alu accesses
    if (curStaticInst->isFloating()){
        numFpAluAccesses++;
        numFpInsts++;
    }
    
    //number of function calls/returns to get window accesses
    if (curStaticInst->isCall() || curStaticInst->isReturn()){
        numCallsReturns++;
    }
    
    //the number of branch predictions that will be made
    if (curStaticInst->isCondCtrl()){
        numCondCtrlInsts++;
    }
    
    //result bus acceses
    if (curStaticInst->isLoad()){
        numLoadInsts++;
    }
    
    if (curStaticInst->isStore()){
        numStoreInsts++;
    }
    /* End power model statistics */

    traceFunctions(instAddr);

    if (traceData) {
        traceData->dump();
        delete traceData;
        traceData = NULL;
    }

    if (curMacroStaticInst && curMacroStaticInst->getName().compare("iret") == 0 && curStaticInst->isLastMicroop() && TheISA::inUserMode(tc)){
        TheISA::CR3 cr3 = tc->readMiscRegNoEffect(MISCREG_CR3);
        Trapframe::TrapFrame *tf = tfs[cr3];
        TheISA::PCState curPcState = tc->pcState();
        if(tf){
            DPRINTF(Zahed, "Current State now: pc [0x%08x] npc [0x%08x] cr3 [0x%08x] edx [0x%08x]\n", curPcState.pc(), curPcState.npc(), cr3, tc->readIntReg(INTREG_EDX));
        }
        TheISA::advancePC(curPcState, curStaticInst);
        if(tf){
            DPRINTF(Zahed, "Current State adv: pc [0x%08x] npc [0x%08x] cr3 [0x%08x] edx [0x%08x]\n", curPcState.pc(), curPcState.npc(), cr3, tc->readIntReg(INTREG_EDX));
        }
        if (tf && (curPcState != tf->pcState)) {
            DPRINTF(Zahed, "Restoring state for CR3 [0x%08x] with pc [0x%08x] npc [0x%08x] and edx [0x%08x]\n", tf->cr3, tf->pcState.pc(), tf->pcState.npc(), tf->intRegs[INTREG_EDX]);
            tc->pcState(tf->pcState);
            tc->setIntReg(INTREG_EAX, tf->intRegs[INTREG_EAX]);
            tc->setIntReg(INTREG_EDX, tf->intRegs[INTREG_EDX]);
            curMacroStaticInst = StaticInst::nullStaticInstPtr;
            fetchOffset = 0;
            restored = true;
        }else if(tf){
            DPRINTF(Zahed, "[0x%08x] MacroInst Matched state for CR3 [0x%08x] with npc [0x%08x] and edx [0x%08x].\n", tc, tf->cr3, tf->pcState.npc(), tf->intRegs[INTREG_EDX]);
        }
        tfs.erase(cr3);
        delete tf;
    }
    return restored;
}


void
BaseSimpleCPU::advancePC(Fault fault)
{
    //Since we're moving to a new pc, zero out the offset
    fetchOffset = 0;
    if (fault != NoFault) {
        curMacroStaticInst = StaticInst::nullStaticInstPtr;
        fault->invoke(tc, curStaticInst);
        predecoder.reset();
    } else {
        if (curStaticInst) {
            if (curStaticInst->isLastMicroop())
                curMacroStaticInst = StaticInst::nullStaticInstPtr;
            TheISA::PCState pcState = thread->pcState();
            TheISA::advancePC(pcState, curStaticInst);
            thread->pcState(pcState);
        }
    }
}

/*Fault
BaseSimpleCPU::CacheOp(uint8_t Op, Addr EffAddr)
{
    // translate to physical address
    Fault fault = NoFault;
    int CacheID = Op & 0x3; // Lower 3 bits identify Cache
    int CacheOP = Op >> 2; // Upper 3 bits identify Cache Operation
    if(CacheID > 1)
      {
        warn("CacheOps not implemented for secondary/tertiary caches\n");
      }
    else
      {
        switch(CacheOP)
          { // Fill Packet Type
          case 0: warn("Invalidate Cache Op\n");
            break;
          case 1: warn("Index Load Tag Cache Op\n");
            break;
          case 2: warn("Index Store Tag Cache Op\n");
            break;
          case 4: warn("Hit Invalidate Cache Op\n");
            break;
          case 5: warn("Fill/Hit Writeback Invalidate Cache Op\n");
            break;
          case 6: warn("Hit Writeback\n");
            break;
          case 7: warn("Fetch & Lock Cache Op\n");
            break;
          default: warn("Unimplemented Cache Op\n");
          }
      }
    return fault;
}*/
