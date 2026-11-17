// algo_evolve.cpp - data that changes from pass to pass (lost-write detector).
#include "algo.h"
#include "gen.h"
#include "inject.h"
#include "lineio.h"
#include "log.h"
#include "order.h"
#include "platform.h"
#include "run.h"
#include "seed.h"
#include <string>
#include <vector>

namespace ddr5 {

// options (the only place evolve reads its keys) `stride` (stride_list), `window_mb`
const std::set<std::string>& EvolveOpts::keys() {
    static const std::set<std::string> k = {"cross",  "flush",  "nt",           "order",
                                            "passes", "period", "verify_order", "width"};
    return k;
}
EvolveOpts EvolveOpts::from(const Params& P, const Logger& log) {
    EvolveOpts o;
    o.write = ospec(P, "order", "random", log);
    o.verify = ospec(P, "verify_order", "seq", log);
    o.strides = stride_list(P, o.write.k == Order::STRIDE || o.verify.k == Order::STRIDE);
    o.width = (int)P.num("width", 32);
    o.nt = P.flag("nt", false);
    o.flush = P.flag("flush", true);
    o.cross = P.flag("cross", false);
    o.passes = std::max<u64>(1, P.num("passes", 16));
    o.period = std::max<u64>(1, P.num("period", 8));
    return o;
}

// algo: evolve (data that changes from pass to pass)
void algo_evolve(Run& R, const Params& P) {
    auto vars = build_vars(P, R.salt, R.rng, R.log); // data = hash: one variant per seed (seeds = N)
    const EvolveOpts opt = EvolveOpts::from(P, R.log);
    const OrderSpec &wo = opt.write, &vo = opt.verify; // wo / vo: write order and verify order
    u64 wl = R.window_lines(P);
    struct Job {
        std::string label;
        u64 seed, stride;
    };
    std::vector<Job> jobs;
    for (auto& v : vars)
        for (u64 s : opt.strides)
            jobs.push_back({"evolve " + v.label +
                                (opt.strides.size() > 1 ? " stride=" + std::to_string(s * kLineBytes) + "B" : ""),
                            v.g.seed, s});
    const u64 base = R.claim_windows(jobs.size());
    R.parallel([&](int tid) {
        for_jobs(R, jobs.size(), [&](size_t ji) {
            const Job& j = jobs[ji];
            if (tid == 0)
                R.set_label(j.label);
            View w = R.view(tid, wl, base + ji), vw = R.view(opt.cross ? (tid + 1) % R.layout.T : tid, wl, base + ji);
            Order ow, ov;
            ow.init(wo.k, w.n, j.stride, 21 + ji, R.salt);
            ov.init(vo.k, vw.n, j.stride, 55 + ji, R.salt);
            const u64 k0 = evo_key(j.seed, 0);
            EvoPass ep;
            ep.init(j.seed, opt.period);
            dispatch(opt.width, opt.nt, opt.flush, [&](auto Wc, auto NTc, auto FLc) {
                using io = IO<decltype(Wc)::value, decltype(NTc)::value, decltype(FLc)::value>;
                walk(R, ow, wo.rev, [&](u64 i) {
                    Line l;
                    evo_make_k(w.g + i, k0, R.rng, l);
                    io::store(w.base + i * kLineBytes, l);
                }); // generation 0 everywhere
                _mm_sfence();
                R.sync();
                for (u64 p = 1; p <= opt.passes && !R.stop; p++) {
                    DDR5_INJECT(evolve_stop_in_write, R, tid, p);
                    ep.set(p);
                    walk(R, ow, wo.rev, [&](u64 i) {
                        u64 g = w.g + i, per = evo_period(g, j.seed, opt.period);
                        if (!ep.is_due(per))
                            return; // not this line's turn: it must keep its old data
                        Line l;
                        evo_make_k(g, ep.key(per), R.rng, l);
                        io::store(w.base + i * kLineBytes, l);
                    });
                    _mm_sfence();
                    R.sync();
                    DDR5_INJECT(evolve_last_pass, R, tid, ji, w, p, opt.passes, j.seed, opt.period, &io::store);
                    if (R.stop)
                        break;
                    walk(R, ov, vo.rev, [&](u64 i) {
                        const u8* a = vw.base + i * kLineBytes;
                        u64 g = vw.g + i;
                        Line e, act;
                        evo_make_k(g, ep.key(evo_period(g, j.seed, opt.period)), R.rng, e);
                        R.vtick();
                        if (!io::verify(a, e, act))
                            R.report(tid, a, e, act, "evolve-verify");
                    });
                    if (!R.stop)
                        R.mark_verified(vw.base, vw.n);
                    R.sync();
                }
            });
        });
    });
}

} // namespace ddr5
