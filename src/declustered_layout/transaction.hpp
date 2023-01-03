#pragma once

#include "graph.hpp"

#include <cstdint>
#include <random>
#include <vector>
#include <cmath>


namespace declustered_layout {


struct Generator {
    std::mt19937 gen;
    std::uniform_real_distribution<> dist{0.0, 1.0};
    Generator() { gen.seed(0); }
    uint64_t operator()() {
        return (uint64_t) 100*tan((M_PI/2)*dist(gen));
    }
};


struct Transaction {
    struct Dependency {
        uint64_t tid1;
        uint64_t tid2;
    };

    inline static Generator gen;
    std::vector<uint64_t> accesses;
    std::vector<Dependency> deps;
    uint64_t repeats = 1;

    Transaction() = default;

    void generate_n(std::size_t n);

    void generate_chain_dep();

    void dependency(uint64_t tid1, uint64_t tid2);

    void rerun(uint64_t times);

    void access(uint64_t tid);
};


} // namespace declustered_layout
