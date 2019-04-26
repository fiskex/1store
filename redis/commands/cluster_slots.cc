#include "redis/commands/cluster_slots.hh"
#include "redis/commands/unexpected.hh"
#include "seastar/core/shared_ptr.hh"
#include "redis/request.hh"
#include "redis/redis_mutation.hh"
#include "redis/reply.hh"
#include "db/system_keyspace.hh"
#include "types.hh"
#include "service/storage_proxy.hh"
#include "service/client_state.hh"
#include "timeout_config.hh"
namespace service {
class storage_proxy;
}
namespace redis {

namespace commands {

static const bytes slots {"slots"};
shared_ptr<abstract_command> cluster_slots::prepare(service::storage_proxy& proxy, const service::client_state& cs, request&& req)
{
    if (req._args_count != 1) {
        return unexpected::prepare(std::move(req._command), std::move(to_bytes(sprint("-wrong number of arguments (given %ld, expected 1)\r\n", req._args_count))));
    }
    auto& slots = req._args[0];
    std::transform(slots.begin(), slots.end(), slots.begin(), ::tolower);
    if (slots != slots) {
        return unexpected::prepare(std::move(req._command), std::move(to_bytes(sprint("-unknown cluster command '%s'\r\n", slots))));
    }
    return seastar::make_shared<cluster_slots> (std::move(req._command));
}

future<redis_message> cluster_slots::execute(service::storage_proxy& proxy, db::consistency_level cl, db::timeout_clock::time_point now, const timeout_config& tc, service::client_state& cs)
{   
    static size_t REDIS_CLUSTER_SLOTS = 16384;
    return db::system_keyspace::load_peers().then([&proxy] (auto&& peers) {
        return gms::inet_address::lookup(proxy.get_db().local().get_config().listen_address()).then([peers = std::move(peers), &proxy] (auto&& local) mutable {
            peers.emplace_back(local);
            auto slots_per_peer = REDIS_CLUSTER_SLOTS / peers.size();
            std::vector<std::tuple<size_t, size_t, bytes, uint16_t>> ret;
            size_t start = 0;
            for (auto& peer : peers) {
                auto end = start + slots_per_peer - 1;
                if (end >= REDIS_CLUSTER_SLOTS) {
                    end = REDIS_CLUSTER_SLOTS - 1;
                }
                auto port = proxy.get_db().local().get_config().redis_transport_port();
                ret.emplace_back(std::tuple<size_t, size_t, bytes, uint16_t> (start, end, to_bytes(peer.to_sstring()), uint16_t { port }));
                start = end + 1;
            }
            return redis_message::make(std::move(ret));
        });
    });
}
}
}