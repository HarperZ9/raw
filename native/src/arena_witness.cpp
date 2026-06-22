#include "raw/arena.hpp"
#include <string>
namespace raw {
std::string arena_witness(const ArenaStats& s){
    auto n = [](std::size_t v){ return std::to_string(v); };
    return "arena budget=" + n(s.budget) + " used=" + n(s.used) +
           " high_water=" + n(s.high_water) + " allocations=" + n(s.allocations) +
           " refusals=" + n(s.refusals) +
           " verdict=" + (s.refusals == 0 ? "BOUNDED" : "BREACHED");
}
}
