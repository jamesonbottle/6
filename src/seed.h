#pragma once
// seed.h - run seed, salt and the selectable hash mixer.
#include "types.h"

namespace ddr5 {

// Every random choice in the engine (order permutations, hash/addr seeds, disturb traffic
u64 salt_for(u64 runSeed, u64 loop); // differs per loop: a long soak never repeats itself
u64 fresh_seed();

// Selectable mixer for 'hash' data (CLI --rng, GUI: Advanced > RNG).
enum class Rng : int { Default = 0, SplitMix, Murmur, XorShift };
inline u64 mix64(u64 x, Rng rng) {
    x += 0x9E3779B97F4A7C15ull;
    switch (rng) {
    case Rng::Murmur:
        x ^= x >> 33;
        x *= 0xff51afd7ed558ccdull;
        x ^= x >> 33;
        x *= 0xc4ceb9fe1a85ec53ull;
        x ^= x >> 33;
        return x;
    // xorshift64* needs a real finalizer: its single multiply only moves information UP
    case Rng::XorShift:
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        x *= 0x2545F4914F6CDD1Dull;
        x ^= x >> 29;
        x *= 0xBF58476D1CE4E5B9ull;
        return x ^ (x >> 32);
    case Rng::Default:
    case Rng::SplitMix:
    default:
        x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ull;
        x = (x ^ (x >> 27)) * 0x94D049BB133111EBull;
        return x ^ (x >> 31);
    }
}

} // namespace ddr5
