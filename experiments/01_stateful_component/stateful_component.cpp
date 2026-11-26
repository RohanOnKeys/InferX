//  Copyright (c) 2026 Rohan Pattanayak
//
//  SPDX-License-Identifier: BSL-1.0

/// \file stateful_component.cpp
/// \brief Stage 1 of InferX: one stateful component under HPX supervision.
///
/// One binary with five roles, started by run_stage1.sh on one node:
///
///   root         work-free locality 0; serves force_disconnect requests
///   host         creates one state_holder component and binds it under the
///                basename "/inferx/state/" with sequence number 0
///   client       joins the host through supervision and runs the probes
///   client2      never joins the host; runs P1 and P4 when the client asks
///   replacement  F5 only: creates its own state_holder and tries to bind the
///                dead host's name, first without and then after unregistering
///
/// The client prints one line per probe:
///   R: scenario=<F> phase=<phase> probe=<P> role=<role> result=<r> t_ms=<t>
///
/// Phases: "before" (all live), "fenced" (client sees the host fenced),
/// "resumed" (F3, F4: after SIGCONT), "removed" (after the root's
/// force_disconnect of the host), "replaced" (F5: after the replacement).
/// Plan: docs/plan.md, section 7.
///
/// Options: --r-role, --r-scenario F0..F5, --r-dir DIR (handshake files),
/// --r-poll-ms N (failure-detection poll timeout, testing hook),
/// --r-bound-s N (probe bound, default 10), --r-inflight-s N (F2 bound,
/// default 30). Everything else goes to HPX.
///
/// The process scaffolding (roles, handshake files, supervision init, root
/// force_disconnect service) follows langX bench/hpx_repro/
/// supervision_gaps_repro.cpp.

#include <hpx/hpx.hpp>
#include <hpx/hpx_start.hpp>
#include <hpx/include/actions.hpp>
#include <hpx/include/components.hpp>
#include <hpx/include/run_as.hpp>
#include <hpx/modules/supervision.hpp>
#include <hpx/runtime_distributed/find_all_localities.hpp>
#include <hpx/runtime_local/get_locality_id.hpp>

#include <hpx/supervision_dispatch.hpp>
#include <hpx/supervision_dispatch/testing.hpp>

#include <sys/stat.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <type_traits>
#include <vector>

///////////////////////////////////////////////////////////////////////////////
/// Resident state: a counter and a vector that live on the host across calls.
struct state_holder : hpx::components::component_base<state_holder>
{
    state_holder()
      : data_(1024)
    {
        for (std::size_t i = 0; i != data_.size(); ++i)
            data_[i] = static_cast<double>(i);
    }

    /// Returns the counter.
    std::int64_t read() const
    {
        return counter_;
    }

    /// Adds \a n to the counter and returns the new value.
    std::int64_t update(std::int64_t n)
    {
        counter_ += n;
        return counter_;
    }

    /// Sleeps \a ms milliseconds, then adds \a n; used for the F2 call that
    /// is in flight when the host is killed.
    std::int64_t slow_update(std::int64_t n, int ms)
    {
        hpx::this_thread::sleep_for(std::chrono::milliseconds(ms));
        return update(n);
    }

    HPX_DEFINE_COMPONENT_ACTION(state_holder, read, read_action)
    HPX_DEFINE_COMPONENT_ACTION(state_holder, update, update_action)
    HPX_DEFINE_COMPONENT_ACTION(state_holder, slow_update, slow_update_action)

private:
    std::int64_t counter_ = 0;
    std::vector<double> data_;
};

using state_holder_type = hpx::components::component<state_holder>;
HPX_REGISTER_COMPONENT(state_holder_type, state_holder)

using read_action = state_holder::read_action;
using update_action = state_holder::update_action;
using slow_update_action = state_holder::slow_update_action;
HPX_REGISTER_ACTION(read_action)
HPX_REGISTER_ACTION(update_action)
HPX_REGISTER_ACTION(slow_update_action)

using fenced_read_action =
    hpx::supervision::make_fenced_action_t<read_action>;
HPX_REGISTER_ACTION_DECLARATION(fenced_read_action)
HPX_REGISTER_ACTION(fenced_read_action)

using fenced_slow_update_action =
    hpx::supervision::make_fenced_action_t<slow_update_action>;
HPX_REGISTER_ACTION_DECLARATION(fenced_slow_update_action)
HPX_REGISTER_ACTION(fenced_slow_update_action)

/// Returns the id of the locality it runs on.
std::uint32_t inferx_ping()
{
    return hpx::get_locality_id();
}
HPX_PLAIN_ACTION(inferx_ping, ping_action)

namespace {

    using clk = std::chrono::steady_clock;
    namespace sv = hpx::supervision;

    char const* const name = "/inferx/state/";
    constexpr std::size_t seq = 0;

    std::string dir, role, scenario;
    int poll_ms = 500;
    int bound_s = 10;
    int inflight_s = 30;
    sv::registry handle;
    std::uint64_t own_epoch = 0;

    std::string arg(int argc, char** argv, char const* key, char const* dflt)
    {
        for (int i = 1; i + 1 < argc; ++i)
            if (std::strcmp(argv[i], key) == 0)
                return argv[i + 1];
        return dflt;
    }

    void say(std::string const& phase, char const* probe,
        std::string const& result, double t_ms)
    {
        std::printf("R: scenario=%s phase=%s probe=%s role=%s result=%s "
                    "t_ms=%.1f\n",
            scenario.c_str(), phase.c_str(), probe, role.c_str(),
            result.c_str(), t_ms);
        std::fflush(stdout);
    }

    std::string file(std::string const& f)
    {
        return dir + "/" + f;
    }

    bool exists(std::string const& f)
    {
        struct stat st{};
        return ::stat(file(f).c_str(), &st) == 0;
    }

    void put(std::string const& f, std::string const& text)
    {
        std::string const tmp = file(f) + ".tmp";
        {
            std::ofstream out(tmp, std::ios::trunc);
            out << text;
        }
        std::rename(tmp.c_str(), file(f).c_str());
    }

    std::string get(std::string const& f)
    {
        std::ifstream in(file(f));
        std::stringstream s;
        s << in.rdbuf();
        return s.str();
    }

    void nap(int ms)
    {
        hpx::this_thread::sleep_for(std::chrono::milliseconds(ms));
    }

    bool wait_file(std::string const& f, int timeout_s)
    {
        auto const deadline = clk::now() + std::chrono::seconds(timeout_s);
        while (clk::now() < deadline)
        {
            if (exists(f))
                return true;
            nap(20);
        }
        return false;
    }

    double ms_since(clk::time_point t0)
    {
        return std::chrono::duration<double, std::milli>(clk::now() - t0)
            .count();
    }

    std::uint32_t loc_of(hpx::id_type const& id)
    {
        return hpx::naming::get_locality_id_from_id(id);
    }

    char const* outcome(sv::dispatch_outcome o)
    {
        return o == sv::dispatch_outcome::admitted ? "admitted" :
            o == sv::dispatch_outcome::rejected_fenced ? "rejected_fenced" :
                                                         "other";
    }

    /// Waits up to \a s seconds for \a f and returns "value:<v>",
    /// "target_fenced", "error:<name>" or "no_answer". Never retries; a
    /// future that is not ready at the bound is abandoned.
    template <typename T>
    std::string settle(hpx::future<T>& f, int s)
    {
        if (f.wait_for(std::chrono::seconds(s)) != hpx::future_status::ready)
            return "no_answer";
        try
        {
            if constexpr (std::is_same_v<T, hpx::id_type>)
                return "id_loc:" + std::to_string(loc_of(f.get()));
            else
                return "value:" + std::to_string(f.get());
        }
        catch (hpx::exception const& e)
        {
            if (hpx::get_error(e) == hpx::error::target_fenced)
                return "target_fenced";
            return std::string("error:") +
                hpx::get_error_name(hpx::get_error(e));
        }
        catch (std::exception const& e)
        {
            return std::string("error:std:") + e.what();
        }
    }

    /// Pings locality  loc with a plain action and prints the result.
    void ping(std::string const& phase, char const* label, std::uint32_t loc)
    {
        auto const t0 = clk::now();
        auto f = hpx::async<ping_action>(
            hpx::naming::get_id_from_locality_id(loc));
        std::string const r = settle(f, bound_s);
        say(phase, label, r, ms_since(t0));
    }

    /// Locality ids this locality currently knows, as "0,2,3,4".
    std::string known_localities()
    {
        std::string s;
        for (auto const& id : hpx::find_all_localities())
            s += (s.empty() ? "" : ",") + std::to_string(loc_of(id));
        return s;
    }

    void init_supervision(int discovery_ms)
    {
        sv::testing::set_failure_detection_poll_timeout_for_testing(
            std::chrono::milliseconds(poll_ms));
        handle = sv::init(
            hpx::launch::sync, std::chrono::milliseconds(discovery_ms));
        own_epoch = sv::query_state(hpx::launch::sync, handle).epoch;
        sv::publish_event(
            hpx::launch::sync, handle, sv::event::running, own_epoch);
    }

    /// Joins until the locality with id \a want is among the joined peers.
    sv::discovered_peer join_locality(std::uint32_t want, int timeout_s)
    {
        auto const deadline = clk::now() + std::chrono::seconds(timeout_s);
        while (clk::now() < deadline)
        {
            for (auto const& p :
                sv::discover_and_join(handle, std::chrono::milliseconds(1000)))
            {
                if (loc_of(p.locality) == want)
                    return p;
            }
        }
        say("setup", "join", "failed", 0);
        std::_Exit(3);
    }

    // ---- roles ---------------------------------------------------------

    void run_root()
    {
        init_supervision(2000);
        put("root.ready", "0");
        for (;;)
        {
            if (exists("root.fd_request") && !exists("root.fd_done"))
            {
                auto const loc = static_cast<std::uint32_t>(
                    std::stoul(get("root.fd_request")));
                hpx::error_code ec(hpx::throwmode::lightweight);
                int const rc = hpx::force_disconnect(
                    hpx::naming::get_id_from_locality_id(loc), ec);
                say("removed", "force_disconnect", ec ?
                        "rc=" + std::to_string(rc) + ",error:" +
                            hpx::get_error_name(static_cast<hpx::error>(ec.value())) :
                        "rc=" + std::to_string(rc),
                    0);
                put("root.fd_done", std::to_string(rc));
            }
            nap(100);
        }
    }

    /// Creates a state_holder here; returns its id.
    hpx::id_type make_state()
    {
        return hpx::new_<state_holder>(hpx::find_here()).get();
    }

    void run_host()
    {
        init_supervision(0);    // joins no peers
        hpx::id_type const id = make_state();
        bool const ok = hpx::register_with_basename(name, id, seq).get();
        put("host.ready", std::to_string(hpx::get_locality_id()));
        say("setup", "bind", ok ? "ok" : "failed", 0);
        for (;;)
            nap(1000);    // keeps id alive
    }

    void run_replacement()
    {
        init_supervision(0);
        hpx::id_type const id = make_state();
        auto const t0 = clk::now();

        // Bind without unregistering the dead host's binding first.
        std::string first;
        try
        {
            first = hpx::register_with_basename(name, id, seq).get() ?
                "bound" :
                "refused";
        }
        catch (hpx::exception const& e)
        {
            first = std::string("error:") +
                hpx::get_error_name(hpx::get_error(e));
        }
        say("replaced", "P6.bind", first, ms_since(t0));

        if (first != "bound")
        {
            auto const t1 = clk::now();
            auto u = hpx::unregister_with_basename(name, seq);
            std::string const r = settle(u, bound_s);
            say("replaced", "P6.unregister", r, ms_since(t1));

            auto const t2 = clk::now();
            std::string second;
            try
            {
                second = hpx::register_with_basename(name, id, seq).get() ?
                    "bound" :
                    "refused";
            }
            catch (hpx::exception const& e)
            {
                second = std::string("error:") +
                    hpx::get_error_name(hpx::get_error(e));
            }
            say("replaced", "P6.rebind", second, ms_since(t2));
        }
        {
            auto const t3 = clk::now();
            auto f = hpx::async<read_action>(id);
            std::string const r = settle(f, bound_s);
            say("replaced", "P6.self", r, ms_since(t3));
        }
        ping("replaced", "P6.ping_root", 0);
        ping("replaced", "P6.ping_client",
            static_cast<std::uint32_t>(std::stoul(get("client.ready"))));
        put("replacement.ready", std::to_string(hpx::get_locality_id()));
        for (;;)
            nap(1000);
    }

    /// P1 and P4, used by both clients.
    void probe_p1(std::string const& phase, hpx::id_type const& comp,
        char const* label = "P1", int bound = 0)
    {
        auto const t0 = clk::now();
        auto f = hpx::async<read_action>(comp);
        std::string const r = settle(f, bound > 0 ? bound : bound_s);
        say(phase, label, r, ms_since(t0));
    }

    void probe_p4(std::string const& phase)
    {
        auto const t0 = clk::now();
        auto f = hpx::find_from_basename(name, seq);
        if (f.wait_for(std::chrono::seconds(bound_s)) !=
            hpx::future_status::ready)
        {
            say(phase, "P4", "no_answer", ms_since(t0));
            return;
        }
        hpx::id_type found;
        try
        {
            found = f.get();
        }
        catch (hpx::exception const& e)
        {
            say(phase, "P4",
                std::string("error:") + hpx::get_error_name(hpx::get_error(e)),
                ms_since(t0));
            return;
        }
        say(phase, "P4", "id_loc:" + std::to_string(loc_of(found)),
            ms_since(t0));
        probe_p1(phase, found, "P4.call");
    }

    void run_client2()
    {
        init_supervision(0);    // never joins the host
        auto c = hpx::find_from_basename(name, seq);
        if (c.wait_for(std::chrono::seconds(60)) != hpx::future_status::ready)
        {
            say("setup", "lookup", "no_answer", 0);
            std::_Exit(3);
        }
        hpx::id_type const comp = c.get();
        put("client2.ready", std::to_string(hpx::get_locality_id()));
        for (int n = 0;; ++n)
        {
            // The client asks with "c2.<n>" holding the phase name.
            std::string const req = "c2." + std::to_string(n);
            while (!exists(req))
                nap(20);
            std::string const phase = get(req);
            probe_p1(phase, comp);
            probe_p4(phase);
            put(req + ".done", "1");
        }
    }

    // ---- client --------------------------------------------------------

    std::uint32_t host_loc = 0;
    sv::discovered_peer host;
    hpx::id_type comp;
    int c2_requests = 0;

    void ask_client2(std::string const& phase)
    {
        std::string const req = "c2." + std::to_string(c2_requests++);
        put(req, phase);
        if (!wait_file(req + ".done", 4 * bound_s + 10))
            say(phase, "client2", "no_answer", 0);
    }

    void probes(std::string const& phase, bool p2_p3 = true)
    {
        probe_p1(phase, comp);
        if (p2_p3)
        {
            auto const t0 = clk::now();
            auto f = sv::dispatch_work<read_action>(comp, host.join_epoch);
            std::string const r = settle(f, bound_s);
            say(phase, "P2", r, ms_since(t0));
            say(phase, "P3",
                outcome(sv::check_admission(host.locality, host.join_epoch)),
                0);
        }
        probe_p4(phase);
        ask_client2(phase);
    }

    /// Waits until the client's view shows the host fenced; prints the
    /// time from the harness marker \a marker to the fence.
    bool wait_fenced(std::string const& marker, int timeout_s)
    {
        wait_file(marker, 60);
        auto const t0 = clk::now();
        while (ms_since(t0) < timeout_s * 1000.0)
        {
            if (sv::check_admission(host.locality, host.join_epoch) ==
                sv::dispatch_outcome::rejected_fenced)
            {
                say("fenced", "detect", "fenced", ms_since(t0));
                return true;
            }
            nap(10);
        }
        say("fenced", "detect", "not_fenced", ms_since(t0));
        return false;
    }

    void remove_host()
    {
        put("root.fd_request", std::to_string(host_loc));
        if (!wait_file("root.fd_done", 30))
            say("removed", "force_disconnect", "no_answer", 0);
        nap(500);
    }

    void run_client()
    {
        init_supervision(2000);
        wait_file("host.ready", 60);
        host_loc = static_cast<std::uint32_t>(std::stoul(get("host.ready")));
        host = join_locality(host_loc, 30);

        auto c = hpx::find_from_basename(name, seq);
        if (c.wait_for(std::chrono::seconds(60)) != hpx::future_status::ready)
        {
            say("setup", "lookup", "no_answer", 0);
            std::_Exit(3);
        }
        comp = c.get();
        auto u = hpx::async<update_action>(comp, std::int64_t(5));
        say("before", "update", settle(u, bound_s), 0);
        put("client.ready", std::to_string(hpx::get_locality_id()));

        wait_file("client2.ready", 60);
        wait_file("go", 60);
        probes("before");
        put("client.armed", "1");

        if (scenario == "F0")
        {
        }
        else if (scenario == "F1")
        {
            wait_fenced("host.killed", 30);
            probes("fenced");
            remove_host();
            probes("removed");
        }
        else if (scenario == "F2")
        {
            auto plain =
                hpx::async<slow_update_action>(comp, std::int64_t(1), 5000);
            auto fenced = sv::dispatch_work<slow_update_action>(
                comp, host.join_epoch, std::int64_t(1), 5000);
            put("client.dispatched", "1");
            wait_fenced("host.killed", 30);
            auto const t0 = clk::now();
            while (ms_since(t0) < inflight_s * 1000.0 &&
                !(plain.is_ready() && fenced.is_ready()))
                nap(50);
            double const waited = ms_since(t0);
            say("fenced", "inflight.plain", settle(plain, 0), waited);
            say("fenced", "inflight.fenced", settle(fenced, 0), waited);
            probes("fenced");
            remove_host();
            probes("removed");
        }
        else if (scenario == "F3")
        {
            wait_fenced("host.stopped", 30);
            probes("fenced");
            put("client.fenced", "1");    // the runner sends SIGCONT
            wait_file("host.resumed", 60);
            nap(1000);
            probes("resumed");
            remove_host();
            probes("removed");
        }
        else if (scenario == "F4")
        {
            wait_file("host.resumed", 60);
            nap(3000);    // several detection windows after SIGCONT
            probes("resumed");
        }
        else if (scenario == "F5")
        {
            wait_fenced("host.killed", 30);
            probe_p4("fenced");
            remove_host();
            probe_p4("removed");
            put("client.want_replacement", "1");
            if (!wait_file("replacement.ready", 60))
                say("replaced", "replacement", "no_answer", 0);
            auto const repl_loc = static_cast<std::uint32_t>(
                std::stoul(get("replacement.ready")));
            say("replaced", "known", known_localities(), 0);
            ping("replaced", "ping_root", 0);
            ping("replaced", "ping", repl_loc);
            probe_p4("replaced");
            {
                auto f = hpx::find_from_basename(name, seq);
                if (f.wait_for(std::chrono::seconds(bound_s)) ==
                    hpx::future_status::ready)
                    probe_p1("replaced", f.get(), "P4.call30", inflight_s);
            }
            ask_client2("replaced");
        }
        say("end", "end", "done", 0);
        put("client.done", "1");
    }
}    // namespace

int main(int argc, char** argv)
{
    role = arg(argc, argv, "--r-role", "client");
    scenario = arg(argc, argv, "--r-scenario", "F0");
    dir = arg(argc, argv, "--r-dir", ".");
    poll_ms = std::stoi(arg(argc, argv, "--r-poll-ms", "500"));
    bound_s = std::stoi(arg(argc, argv, "--r-bound-s", "10"));
    inflight_s = std::stoi(arg(argc, argv, "--r-inflight-s", "30"));

    std::vector<char*> hargs{argv[0]};
    for (int i = 1; i < argc; ++i)
    {
        if (std::strncmp(argv[i], "--r-", 4) == 0)
        {
            ++i;
            continue;
        }
        hargs.push_back(argv[i]);
    }
    int hargc = static_cast<int>(hargs.size());
    hargs.push_back(nullptr);

    hpx::init_params params;
    if (role == "root")
    {
        params.mode = hpx::runtime_mode::console;
        params.cfg = {"hpx.expect_connecting_localities!=1"};
    }
    else
    {
        params.mode = hpx::runtime_mode::connect;
    }
    if (!hpx::start(nullptr, hargc, hargs.data(), params))
        return 2;

    hpx::run_as_hpx_thread([] {
        if (role == "root")
            run_root();
        else if (role == "host")
            run_host();
        else if (role == "client2")
            run_client2();
        else if (role == "replacement")
            run_replacement();
        else
            run_client();
    });

    std::_Exit(0);    // shutdown is not what stage 1 measures
}
