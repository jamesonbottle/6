#pragma once
// cpu_set.h - the set of CPUs a process may run on, as plain data (header only, no OS calls
#include <vector>
#include "types.h"

namespace ddr5 {
namespace cpuset {

struct GroupMask {
    unsigned group = 0;
    u64 mask = 0; // bit i set = logical CPU i of this group is allowed
};
struct CpuRef {
    unsigned group = 0;
    unsigned bit = 0;
    bool operator==(const CpuRef& o) const { return group == o.group && bit == o.bit; }
};

// The mask with the lowest n bits set; n >= 64 gives all 64 (a shift by 64 would be undefined).
inline u64 low_bits(unsigned n) {
    return n >= 64 ? ~0ull : ((1ull << n) - 1);
}

// Every allowed CPU, in group order and bit order.
inline std::vector<CpuRef> expand(const std::vector<GroupMask>& groups) {
    std::vector<CpuRef> v;
    for (const GroupMask& g : groups)
        for (unsigned b = 0; b < 64; b++)
            if ((g.mask >> b) & 1)
                v.push_back(CpuRef{g.group, b});
    return v;
}

// `allowed` confined to what `limit` permits (a job object that lists groups and CPUs)
inline std::vector<GroupMask> restrict_to(const std::vector<GroupMask>& allowed, const std::vector<GroupMask>& limit) {
    std::vector<GroupMask> out;
    for (const GroupMask& a : allowed) {
        u64 m = 0;
        for (const GroupMask& l : limit)
            if (l.group == a.group)
                m |= l.mask;
        m &= a.mask;
        if (m)
            out.push_back(GroupMask{a.group, m});
    }
    return out;
}

// The CPU for thread number `idx`: the idx-th allowed CPU
inline const CpuRef* nth(const std::vector<CpuRef>& cpus, int idx) {
    if (cpus.empty() || idx < 0)
        return nullptr;
    return &cpus[(size_t)idx % cpus.size()];
}

} // namespace cpuset
} // namespace ddr5
