/*
 * Copyright (c) 1999-2008 Mark D. Hill and David A. Wood
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
 */

#include "base/intmath.hh"
#include "debug/RubyCache.hh"
#include "mem/ruby/system/CacheMemory.hh"

using namespace std;

ostream&
operator<<(ostream& out, const CacheMemory& obj)
{
    obj.print(out);
    out << flush;
    return out;
}

CacheMemory *
RubyCacheParams::create()
{
    return new CacheMemory(this);
}

CacheMemory::CacheMemory(const Params *p)
    : SimObject(p)
{
    m_cache_size = p->size;
    m_latency = p->latency;
    m_cache_assoc = p->assoc;
    m_policy = p->replacement_policy;
    m_profiler_ptr = new CacheProfiler(name());
    m_start_index_bit = p->start_index_bit;
}

void
CacheMemory::init()
{
    m_cache_num_sets = (m_cache_size / m_cache_assoc) /
        RubySystem::getBlockSizeBytes();
    assert(m_cache_num_sets > 1);
    m_cache_num_set_bits = floorLog2(m_cache_num_sets);
    assert(m_cache_num_set_bits > 0);

    if (m_policy == "PSEUDO_LRU")
        m_replacementPolicy_ptr =
            new PseudoLRUPolicy(m_cache_num_sets, m_cache_assoc);
    else if (m_policy == "LRU")
        m_replacementPolicy_ptr =
            new LRUPolicy(m_cache_num_sets, m_cache_assoc);
    else
        assert(false);

    m_cache.resize(m_cache_num_sets);
    for (int i = 0; i < m_cache_num_sets; i++) {
        m_cache[i].resize(m_cache_assoc);
        for (int j = 0; j < m_cache_assoc; j++) {
            m_cache[i][j] = NULL;
        }
    }
}

CacheMemory::~CacheMemory()
{
    if (m_replacementPolicy_ptr != NULL)
        delete m_replacementPolicy_ptr;
    delete m_profiler_ptr;
    for (int i = 0; i < m_cache_num_sets; i++) {
        for (int j = 0; j < m_cache_assoc; j++) {
            delete m_cache[i][j];
        }
    }
}

void
CacheMemory::printConfig(ostream& out)
{
    int block_size = RubySystem::getBlockSizeBytes();

    out << "Cache config: " << m_cache_name << endl;
    out << "  cache_associativity: " << m_cache_assoc << endl;
    out << "  num_cache_sets_bits: " << m_cache_num_set_bits << endl;
    const int cache_num_sets = 1 << m_cache_num_set_bits;
    out << "  num_cache_sets: " << cache_num_sets << endl;
    out << "  cache_set_size_bytes: " << cache_num_sets * block_size << endl;
    out << "  cache_set_size_Kbytes: "
        << double(cache_num_sets * block_size) / (1<<10) << endl;
    out << "  cache_set_size_Mbytes: "
        << double(cache_num_sets * block_size) / (1<<20) << endl;
    out << "  cache_size_bytes: "
        << cache_num_sets * block_size * m_cache_assoc << endl;
    out << "  cache_size_Kbytes: "
        << double(cache_num_sets * block_size * m_cache_assoc) / (1<<10)
        << endl;
    out << "  cache_size_Mbytes: "
        << double(cache_num_sets * block_size * m_cache_assoc) / (1<<20)
        << endl;
}

// convert a Address to its location in the cache
Index
CacheMemory::addressToCacheSet(const Address& address) const
{
    assert(address == line_address(address));
    return address.bitSelect(m_start_index_bit,
                             m_start_index_bit + m_cache_num_set_bits - 1);
}

// Given a cache index: returns the index of the tag in a set.
// returns -1 if the tag is not found.
int
CacheMemory::findTagInSet(Index cacheSet, const Address& tag) const
{
    assert(tag == line_address(tag));
    // search the set for the tags
    m5::hash_map<Address, int>::const_iterator it = m_tag_index.find(tag);
    if (it != m_tag_index.end())
        if (m_cache[cacheSet][it->second]->m_Permission !=
            AccessPermission_NotPresent)
            return it->second;
    return -1; // Not found
}

// Given a cache index: returns the index of the tag in a set.
// returns -1 if the tag is not found.
int
CacheMemory::findTagInSetIgnorePermissions(Index cacheSet,
                                           const Address& tag) const
{
    assert(tag == line_address(tag));
    // search the set for the tags
    m5::hash_map<Address, int>::const_iterator it = m_tag_index.find(tag);
    if (it != m_tag_index.end())
        return it->second;
    return -1; // Not found
}

bool
CacheMemory::tryCacheAccess(const Address& address, RubyRequestType type,
                            DataBlock*& data_ptr)
{
    assert(address == line_address(address));
    DPRINTF(RubyCache, "address: %s\n", address);
    Index cacheSet = addressToCacheSet(address);
    int loc = findTagInSet(cacheSet, address);
    if (loc != -1) {
        // Do we even have a tag match?
        AbstractCacheEntry* entry = m_cache[cacheSet][loc];
        m_replacementPolicy_ptr->
            touch(cacheSet, loc, g_eventQueue_ptr->getTime());
        data_ptr = &(entry->getDataBlk());

        if (entry->m_Permission == AccessPermission_Read_Write) {
            return true;
        }
        if ((entry->m_Permission == AccessPermission_Read_Only) &&
            (type == RubyRequestType_LD || type == RubyRequestType_IFETCH)) {
            return true;
        }
        // The line must not be accessible
    }
    data_ptr = NULL;
    return false;
}

bool
CacheMemory::testCacheAccess(const Address& address, RubyRequestType type,
                             DataBlock*& data_ptr)
{
    assert(address == line_address(address));
    DPRINTF(RubyCache, "address: %s\n", address);
    Index cacheSet = addressToCacheSet(address);
    int loc = findTagInSet(cacheSet, address);

    if (loc != -1) {
        // Do we even have a tag match?
        AbstractCacheEntry* entry = m_cache[cacheSet][loc];
        m_replacementPolicy_ptr->
            touch(cacheSet, loc, g_eventQueue_ptr->getTime());
        data_ptr = &(entry->getDataBlk());

        return m_cache[cacheSet][loc]->m_Permission !=
            AccessPermission_NotPresent;
    }

    data_ptr = NULL;
    return false;
}

// tests to see if an address is present in the cache
bool
CacheMemory::isTagPresent(const Address& address) const
{
    assert(address == line_address(address));
    Index cacheSet = addressToCacheSet(address);
    int loc = findTagInSet(cacheSet, address);

    if (loc == -1) {
        // We didn't find the tag
        DPRINTF(RubyCache, "No tag match for address: %s\n", address);
        return false;
    }
    DPRINTF(RubyCache, "address: %s found\n", address);
    return true;
}

// Returns true if there is:
//   a) a tag match on this address or there is
//   b) an unused line in the same cache "way"
bool
CacheMemory::cacheAvail(const Address& address) const
{
    assert(address == line_address(address));

    Index cacheSet = addressToCacheSet(address);

    for (int i = 0; i < m_cache_assoc; i++) {
        AbstractCacheEntry* entry = m_cache[cacheSet][i];
        if (entry != NULL) {
            if (entry->m_Address == address ||
                entry->m_Permission == AccessPermission_NotPresent) {
                // Already in the cache or we found an empty entry
                return true;
            }
        } else {
            return true;
        }
    }
    return false;
}

AbstractCacheEntry*
CacheMemory::allocate(const Address& address, AbstractCacheEntry* entry)
{
    assert(address == line_address(address));
    assert(!isTagPresent(address));
    assert(cacheAvail(address));
    DPRINTF(RubyCache, "address: %s\n", address);

    // Find the first open slot
    Index cacheSet = addressToCacheSet(address);
    std::vector<AbstractCacheEntry*> &set = m_cache[cacheSet];
    for (int i = 0; i < m_cache_assoc; i++) {
        if (!set[i] || set[i]->m_Permission == AccessPermission_NotPresent) {
            set[i] = entry;  // Init entry
            set[i]->m_Address = address;
            set[i]->m_Permission = AccessPermission_Invalid;
            DPRINTF(RubyCache, "Allocate clearing lock for addr: %x\n",
                    address);
            set[i]->m_locked = -1;
            m_tag_index[address] = i;

            m_replacementPolicy_ptr->
                touch(cacheSet, i, g_eventQueue_ptr->getTime());

            return entry;
        }
    }
    panic("Allocate didn't find an available entry");
}

void
CacheMemory::deallocate(const Address& address)
{
    assert(address == line_address(address));
    assert(isTagPresent(address));
    DPRINTF(RubyCache, "address: %s\n", address);
    Index cacheSet = addressToCacheSet(address);
    int loc = findTagInSet(cacheSet, address);
    if (loc != -1) {
        delete m_cache[cacheSet][loc];
        m_cache[cacheSet][loc] = NULL;
        m_tag_index.erase(address);
    }
}

// Returns with the physical address of the conflicting cache line
Address
CacheMemory::cacheProbe(const Address& address) const
{
    assert(address == line_address(address));
    assert(!cacheAvail(address));

    Index cacheSet = addressToCacheSet(address);
    return m_cache[cacheSet][m_replacementPolicy_ptr->getVictim(cacheSet)]->
        m_Address;
}

// looks an address up in the cache
AbstractCacheEntry*
CacheMemory::lookup(const Address& address)
{
    assert(address == line_address(address));
    Index cacheSet = addressToCacheSet(address);
    int loc = findTagInSet(cacheSet, address);
    if(loc == -1) return NULL;
    return m_cache[cacheSet][loc];
}

// looks an address up in the cache
const AbstractCacheEntry*
CacheMemory::lookup(const Address& address) const
{
    assert(address == line_address(address));
    Index cacheSet = addressToCacheSet(address);
    int loc = findTagInSet(cacheSet, address);
    if(loc == -1) return NULL;
    return m_cache[cacheSet][loc];
}

// Sets the most recently used bit for a cache block
void
CacheMemory::setMRU(const Address& address)
{
    Index cacheSet;

    cacheSet = addressToCacheSet(address);
    m_replacementPolicy_ptr->
        touch(cacheSet, findTagInSet(cacheSet, address),
              g_eventQueue_ptr->getTime());
}

void
CacheMemory::profileMiss(const RubyRequest& msg)
{
    m_profiler_ptr->addCacheStatSample(msg.getType(), 
                                       msg.getAccessMode(),
                                       msg.getPrefetch());
}

void
CacheMemory::profileGenericRequest(GenericRequestType requestType,
                                   RubyAccessMode accessType,
                                   PrefetchBit pfBit)
{
    m_profiler_ptr->addGenericStatSample(requestType, 
                                         accessType, 
                                         pfBit);
}

void
CacheMemory::recordCacheContents(CacheRecorder& tr) const
{
    for (int i = 0; i < m_cache_num_sets; i++) {
        for (int j = 0; j < m_cache_assoc; j++) {
            AccessPermission perm = m_cache[i][j]->m_Permission;
            RubyRequestType request_type = RubyRequestType_NULL;
            if (perm == AccessPermission_Read_Only) {
                if (m_is_instruction_only_cache) {
                    request_type = RubyRequestType_IFETCH;
                } else {
                    request_type = RubyRequestType_LD;
                }
            } else if (perm == AccessPermission_Read_Write) {
                request_type = RubyRequestType_ST;
            }

            if (request_type != RubyRequestType_NULL) {
#if 0
                tr.addRecord(m_chip_ptr->getID(), m_cache[i][j].m_Address,
                             Address(0), request_type,
                             m_replacementPolicy_ptr->getLastAccess(i, j));
#endif
            }
        }
    }
}

void
CacheMemory::print(ostream& out) const
{
    out << "Cache dump: " << m_cache_name << endl;
    for (int i = 0; i < m_cache_num_sets; i++) {
        for (int j = 0; j < m_cache_assoc; j++) {
            if (m_cache[i][j] != NULL) {
                out << "  Index: " << i
                    << " way: " << j
                    << " entry: " << *m_cache[i][j] << endl;
            } else {
                out << "  Index: " << i
                    << " way: " << j
                    << " entry: NULL" << endl;
            }
        }
    }
}

void
CacheMemory::printData(ostream& out) const
{
    out << "printData() not supported" << endl;
}

void
CacheMemory::clearStats() const
{
    m_profiler_ptr->clearStats();
}

void
CacheMemory::printStats(ostream& out) const
{
    m_profiler_ptr->printStats(out);
}

void
CacheMemory::setLocked(const Address& address, int context)
{
    DPRINTF(RubyCache, "Setting Lock for addr: %x to %d\n", address, context);
    assert(address == line_address(address));
    Index cacheSet = addressToCacheSet(address);
    int loc = findTagInSet(cacheSet, address);
    assert(loc != -1);
    m_cache[cacheSet][loc]->m_locked = context;
}

void
CacheMemory::clearLocked(const Address& address)
{
    DPRINTF(RubyCache, "Clear Lock for addr: %x\n", address);
    assert(address == line_address(address));
    Index cacheSet = addressToCacheSet(address);
    int loc = findTagInSet(cacheSet, address);
    assert(loc != -1);
    m_cache[cacheSet][loc]->m_locked = -1;
}

bool
CacheMemory::isLocked(const Address& address, int context)
{
    assert(address == line_address(address));
    Index cacheSet = addressToCacheSet(address);
    int loc = findTagInSet(cacheSet, address);
    assert(loc != -1);
    DPRINTF(RubyCache, "Testing Lock for addr: %llx cur %d con %d\n",
            address, m_cache[cacheSet][loc]->m_locked, context);
    return m_cache[cacheSet][loc]->m_locked == context;
}

