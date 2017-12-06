/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Microsoft Corporation
 *
 * -=- Robust Distributed System Nucleus (rDSN) -=-
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 * Description:
 *     replication ddl client
 *
 * Revision history:
 *     2015-12-30, xiaotz, first version
 */

#pragma once

#include <cctype>
#include <dsn/dist/replication.h>

namespace dsn {
namespace replication {

class replication_ddl_client : public clientlet
{
public:
    replication_ddl_client(const std::vector<dsn::rpc_address> &meta_servers);
    virtual ~replication_ddl_client();

    dsn::error_code create_app(const std::string &app_name,
                               const std::string &app_type,
                               int partition_count,
                               int replica_count,
                               const std::map<std::string, std::string> &envs,
                               bool is_stateless);

    // reserve_seconds == 0 means use default value in configuration 'hold_seconds_for_dropped_app'
    dsn::error_code drop_app(const std::string &app_name, int reserve_seconds);

    dsn::error_code recall_app(int32_t app_id, const std::string &new_app_name);

    dsn::error_code list_apps(const dsn::app_status::type status,
                              bool show_all,
                              bool detailed,
                              const std::string &file_name);

    dsn::error_code list_apps(const dsn::app_status::type status,
                              std::vector<::dsn::app_info> &apps);

    dsn::error_code list_nodes(const dsn::replication::node_status::type status,
                               bool detailed,
                               const std::string &file_name,
                               bool resolve_ip = false);

    dsn::error_code
    list_nodes(const dsn::replication::node_status::type status,
               std::map<dsn::rpc_address, dsn::replication::node_status::type> &nodes);

    dsn::error_code cluster_info(const std::string &file_name, bool resolve_ip = false);

    dsn::error_code list_app(const std::string &app_name,
                             bool detailed,
                             const std::string &file_name,
                             bool resolve_ip = false);

    dsn::error_code list_app(const std::string &app_name,
                             int32_t &app_id,
                             int32_t &partition_count,
                             std::vector<partition_configuration> &partitions);

    dsn::replication::configuration_meta_control_response
    control_meta_function_level(meta_function_level::type level);

    dsn::error_code send_balancer_proposal(const configuration_balancer_request &request);

    dsn::error_code
    wait_app_ready(const std::string &app_name, int partition_count, int max_replica_count);

    dsn::error_code do_recovery(const std::vector<dsn::rpc_address> &replica_nodes,
                                int wait_seconds,
                                bool skip_bad_nodes,
                                bool skip_lost_partitions,
                                const std::string &outfile);

    // get host name from ip series
    // if can't get a hostname from ip(maybe no hostname or other errors), return UNRESOLVABLE
    // if multiple hostname got, return <host1,host2> ...
    // we only support ipv4 currently

    // ip_network_order -> hostname
    static std::string hostname_from_ip(uint32_t ip);
    // a.b.c.d -> hostname
    static std::string hostname_from_ip(const char *ip);
    // a.b.c.d:port1 -> hostname:port1
    static std::string hostname_from_ip_port(const char *ip_port);
    // ipv4_rpc_address -> hostname:port | non_ipv4 -> "invalid"
    static std::string hostname(const dsn::rpc_address &address);
    // a.b.c.d,e.f.g.h -> hostname1,hostname2
    static std::string list_hostname_from_ip(const char *ip_list);
    // a.b.c.d:port1,e.f.g.h:port2 -> hostname1:port2,hostname2:port2
    static std::string list_hostname_from_ip_port(const char *ip_port_list);

private:
    bool static valid_app_char(int c);

    void end_meta_request(task_ptr callback,
                          int retry_times,
                          error_code err,
                          dsn_message_t request,
                          dsn_message_t resp);

    template <typename TRequest>
    dsn::task_ptr request_meta(dsn_task_code_t code,
                               std::shared_ptr<TRequest> &req,
                               int timeout_milliseconds = 0,
                               int reply_thread_hash = 0)
    {
        dsn_message_t msg = dsn_msg_create_request(code, timeout_milliseconds);
        task_ptr task = ::dsn::rpc::create_rpc_response_task(
            msg, nullptr, [](error_code err, dsn_message_t, dsn_message_t) {}, reply_thread_hash);
        ::dsn::marshall(msg, *req);
        rpc::call(_meta_server,
                  msg,
                  this,
                  [this, task](error_code err, dsn_message_t request, dsn_message_t response) {
                      end_meta_request(std::move(task), 0, err, request, response);
                  });
        return task;
    }

private:
    dsn::rpc_address _meta_server;
};
}
} // namespace
