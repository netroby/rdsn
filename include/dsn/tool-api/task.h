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
 *     the task abstraction in zion, as well as the derived various types of
 *     tasks in our system
 *
 * Revision history:
 *     Mar., 2015, @imzhenyu (Zhenyu Guo), first version
 *     xxxx-xx-xx, author, fix bug about xxx
 */

#pragma once

#include <functional>
#include <dsn/utility/ports.h>
#include <dsn/utility/extensible_object.h>
#include <dsn/utility/callocator.h>
#include <dsn/utility/utils.h>
#include <dsn/utility/mm.h>
#include <dsn/tool-api/task_spec.h>
#include <dsn/tool-api/task_tracker.h>
#include <dsn/tool-api/rpc_message.h>
#include <dsn/tool-api/auto_codes.h>

#ifdef __TITLE__
#undef __TITLE__
#endif
#define __TITLE__ "task"

namespace dsn {

class task_worker;
class task_worker_pool;
class service_node;
class task_engine;
class task_queue;
class rpc_engine;
class disk_engine;
class env_provider;
class nfs_node;
class timer_service;
class task;

struct __tls_dsn__
{
    uint32_t magic;
    task *current_task;

    task_worker *worker;
    int worker_index;
    service_node *node;
    int node_id;

    rpc_engine *rpc;
    disk_engine *disk;
    env_provider *env;
    nfs_node *nfs;
    timer_service *tsvc;

    int last_worker_queue_size;
    uint64_t node_pool_thread_ids; // 8,8,16 bits
    uint32_t last_lower32_task_id; // 32bits

    char scratch_buffer[4][256]; // for temp to_string() etc., 4 buffers in maximum
    int scratch_buffer_index;
    char *scratch_next() { return scratch_buffer[++scratch_buffer_index % 4]; }
};

extern __thread struct __tls_dsn__ tls_dsn;

//----------------- common task -------------------------------------------------------
class task : public ref_counter, public extensible_object<task, 4>
{
public:
    DSN_API task(dsn::task_code code, int hash = 0, service_node *node = nullptr);
    DSN_API virtual ~task();

    virtual void exec() = 0;

    DSN_API void exec_internal();
    // return whether *this* cancel success,
    // for timers, even return value is false, the further timer execs are cancelled
    DSN_API bool cancel(bool wait_until_finished, /*out*/ bool *finished = nullptr);
    DSN_API bool wait(int timeout_milliseconds = TIME_MS_MAX, bool on_cancel = false);
    DSN_API void enqueue(std::chrono::milliseconds delay)
    {
        set_delay(static_cast<int>(delay.count()));
        enqueue();
    }
    DSN_API virtual void enqueue();
    DSN_API bool set_retry(
        bool enqueue_immediately = true); // return true when called inside exec(), false otherwise
    void set_error_code(error_code err) { _error = err; }
    void set_delay(int delay_milliseconds = 0) { _delay_milliseconds = delay_milliseconds; }
    void set_tracker(task_tracker *tracker) { _context_tracker.set_tracker(tracker, this); }

    uint64_t id() const { return _task_id; }
    task_state state() const { return _state.load(std::memory_order_acquire); }
    dsn::task_code code() const { return _spec->code; }
    task_spec &spec() const { return *_spec; }
    int hash() const { return _hash; }
    int delay_milliseconds() const { return _delay_milliseconds; }
    error_code error() const { return _error; }
    service_node *node() const { return _node; }
    task_tracker *tracker() const { return _context_tracker.tracker(); }
    bool is_empty() const { return _is_null; }

    // static helper utilities
    static task *get_current_task();
    static uint64_t get_current_task_id();
    static task_worker *get_current_worker();
    static task_worker *get_current_worker2();
    static service_node *get_current_node();
    static service_node *get_current_node2();
    static int get_current_node_id();
    static int get_current_worker_index();
    static const char *get_current_node_name();
    static rpc_engine *get_current_rpc();
    static disk_engine *get_current_disk();
    static env_provider *get_current_env();
    static nfs_node *get_current_nfs();
    static timer_service *get_current_tsvc();

    DSN_API static void set_tls_dsn_context(
        service_node *node, // cannot be null
        task_worker *worker // null for io or timer threads if they are not worker threads
        );

protected:
    DSN_API void signal_waiters();
    DSN_API void enqueue(task_worker_pool *pool);
    void set_task_id(uint64_t tid) { _task_id = tid; }

    bool _is_null;
    error_code _error;

private:
    task(const task &);
    static void check_tls_dsn();
    static void on_tls_dsn_not_set();

    mutable std::atomic<task_state> _state;
    uint64_t _task_id;
    std::atomic<void *> _wait_event;
    int _hash;
    int _delay_milliseconds;
    bool _wait_for_cancel;
    task_spec *_spec;
    service_node *_node;
    trackable_task _context_tracker; // when tracker is gone, the task is cancelled automatically

public:
    // used by task queue only
    task *next;
};
typedef dsn::ref_ptr<dsn::task> task_ptr;

class raw_task : public task, public transient_object
{
public:
    raw_task(dsn::task_code code,
             const task_handler &cb,
             int hash = 0,
             service_node *node = nullptr)
        : task(code, hash, node), _cb(cb)
    {
    }
    raw_task(dsn::task_code code, task_handler &&cb, int hash = 0, service_node *node = nullptr)
        : task(code, hash, node), _cb(std::move(cb))
    {
    }
    void exec() override
    {
        if (_cb != nullptr) {
            _cb();
            _cb = nullptr;
        }
    }

protected:
    task_handler _cb;
};

//----------------- timer task -------------------------------------------------------

class timer_task : public task
{
public:
    timer_task(dsn::task_code code,
               const task_handler &cb,
               uint32_t interval_milliseconds,
               int hash = 0,
               service_node *node = nullptr);
    timer_task(dsn::task_code code,
               task_handler &&cb,
               uint32_t interval_milliseconds,
               int hash = 0,
               service_node *node = nullptr);

    DSN_API void exec() override;

    DSN_API void enqueue() override;

private:
    uint32_t _interval_milliseconds;
    task_handler _cb;
};

template <typename TCallback>
class safe_late_task : public raw_task
{
public:
    typedef std::function<task_handler(TCallback &)> currying;
    safe_late_task(task_code code, const TCallback &cb, int hash = 0, service_node *node = nullptr)
        : raw_task(code, nullptr, hash, node), _user_cb(cb)
    {
    }
    safe_late_task(task_code code, TCallback &&cb, int hash = 0, service_node *node = nullptr)
        : raw_task(code, nullptr, hash, node), _user_cb(std::move(cb))
    {
    }

    void bind_and_enqueue(const currying &c, int delay_ms = 0)
    {
        if (_user_cb != nullptr) {
            raw_task::_cb = c(_user_cb);
        }
        set_delay(delay_ms);
        enqueue();
    }

private:
    TCallback _user_cb;
};

class rpc_request_task : public task, public transient_object
{
public:
    rpc_request_task(message_ex *request, rpc_request_handler &&h, service_node *node);
    ~rpc_request_task();

    message_ex *get_request() const { return _request; }

    DSN_API void enqueue() override;

    void exec() override
    {
        if (0 == _enqueue_ts_ns ||
            dsn_now_ns() - _enqueue_ts_ns <
                static_cast<uint64_t>(_request->header->client.timeout_ms) * 1000000ULL) {
            if (_handler != nullptr)
                _handler(_request);
        } else {
            dwarn("rpc_request_task(%s) from(%s) stop to execute due to timeout_ms(%d) exceed",
                  spec().name.c_str(),
                  _request->header->from_address.to_string(),
                  _request->header->client.timeout_ms);
        }
        _handler = nullptr;
    }

protected:
    message_ex *_request;
    rpc_request_handler _handler;
    uint64_t _enqueue_ts_ns;
};
typedef dsn::ref_ptr<rpc_request_task> rpc_request_task_ptr;

class rpc_response_task : public task, public transient_object
{
public:
    DSN_API rpc_response_task(message_ex *request,
                              const rpc_response_handler &cb,
                              int hash = 0,
                              service_node *node = nullptr);
    DSN_API rpc_response_task(message_ex *request,
                              rpc_response_handler &&cb,
                              int hash,
                              service_node *node = nullptr);
    DSN_API ~rpc_response_task();

    // return true for normal case, false for fault injection applied
    DSN_API bool enqueue(error_code err, message_ex *reply);
    DSN_API void enqueue() override; // re-enqueue after above enqueue, e.g., after delay
    message_ex *get_request() const { return _request; }
    message_ex *get_response() const { return _response; }

    // used only when replace_callback is called before, not thread-safe
    DSN_API const rpc_response_handler &current_handler() const { return _cb; }
    DSN_API void reset_callback(const rpc_response_handler &cb);
    DSN_API void reset_callback(rpc_response_handler &&cb);

    task_worker_pool *caller_pool() const { return _caller_pool; }
    void set_caller_pool(task_worker_pool *pl) { _caller_pool = pl; }

    void exec() override
    {
        if (_cb) {
            _cb(_error, _request, _response);
        }
    }

private:
    message_ex *_request;
    message_ex *_response;
    task_worker_pool *_caller_pool;
    rpc_response_handler _cb;

    friend class rpc_engine;
};
typedef dsn::ref_ptr<rpc_response_task> rpc_response_task_ptr;

//------------------------- disk AIO task ---------------------------------------------------

enum aio_type
{
    AIO_Invalid,
    AIO_Read,
    AIO_Write
};

class disk_engine;
class disk_aio
{
public:
    // filled by apps
    dsn_handle_t file;
    void *buffer;
    uint32_t buffer_size;
    uint64_t file_offset;

    // filled by frameworks
    aio_type type;
    disk_engine *engine;
    void *file_object;

    disk_aio()
        : file(nullptr),
          buffer(nullptr),
          buffer_size(0),
          file_offset(0),
          type(AIO_Invalid),
          engine(nullptr),
          file_object(nullptr)
    {
    }
    virtual ~disk_aio() {}
};

class aio_task : public task, public transient_object
{
public:
    DSN_API aio_task(dsn::task_code code,
                     const aio_handler &cb,
                     int hash = 0,
                     service_node *node = nullptr);
    DSN_API
    aio_task(dsn::task_code code, aio_handler &&cb, int hash = 0, service_node *node = nullptr);
    DSN_API ~aio_task();

    DSN_API void enqueue_aio(error_code err, size_t transferred_size);
    size_t get_transferred_size() const { return _transferred_size; }
    disk_aio *aio() { return _aio; }

    void copy_to(char *dest)
    {
        if (!_unmerged_write_buffers.empty()) {
            for (auto &buffer : _unmerged_write_buffers) {
                memcpy(dest, buffer.buffer, buffer.size);
                dest += buffer.size;
            }
        } else {
            memcpy(dest, _aio->buffer, _aio->buffer_size);
        }
    }

    void collapse()
    {
        if (!_unmerged_write_buffers.empty()) {
            std::shared_ptr<char> buffer(dsn::make_shared_array<char>(_aio->buffer_size));
            _merged_write_buffer_holder.assign(buffer, 0, _aio->buffer_size);
            _aio->buffer = buffer.get();
            copy_to(buffer.get());
        }
    }

    virtual void exec() override // aio completed
    {
        if (nullptr != _cb) {
            _cb(_error, _transferred_size);
            _cb = nullptr;
        }
    }

    std::vector<dsn_file_buffer_t> _unmerged_write_buffers;
    blob _merged_write_buffer_holder;

protected:
    disk_aio *_aio;
    size_t _transferred_size;
    aio_handler _cb;
};
typedef dsn::ref_ptr<aio_task> aio_task_ptr;

// ------------------------ inline implementations --------------------
__inline /*static*/ void task::check_tls_dsn()
{
    if (tls_dsn.magic != 0xdeadbeef) {
        on_tls_dsn_not_set();
    }
}

__inline /*static*/ task *task::get_current_task()
{
    check_tls_dsn();
    return tls_dsn.current_task;
}

__inline /*static*/ uint64_t task::get_current_task_id()
{
    if (tls_dsn.magic == 0xdeadbeef)
        return tls_dsn.current_task ? tls_dsn.current_task->id() : 0;
    else
        return 0;
}

__inline /*static*/ task_worker *task::get_current_worker()
{
    check_tls_dsn();
    return tls_dsn.worker;
}

__inline /*static*/ task_worker *task::get_current_worker2()
{
    return tls_dsn.magic == 0xdeadbeef ? tls_dsn.worker : nullptr;
}

__inline /*static*/ service_node *task::get_current_node()
{
    check_tls_dsn();
    return tls_dsn.node;
}

__inline /*static*/ int task::get_current_node_id()
{
    return tls_dsn.magic == 0xdeadbeef ? tls_dsn.node_id : 0;
}

__inline /*static*/ service_node *task::get_current_node2()
{
    return tls_dsn.magic == 0xdeadbeef ? tls_dsn.node : nullptr;
}

__inline /*static*/ int task::get_current_worker_index()
{
    check_tls_dsn();
    return tls_dsn.worker_index;
}

__inline /*static*/ rpc_engine *task::get_current_rpc()
{
    check_tls_dsn();
    return tls_dsn.rpc;
}

__inline /*static*/ disk_engine *task::get_current_disk()
{
    check_tls_dsn();
    return tls_dsn.disk;
}

__inline /*static*/ env_provider *task::get_current_env()
{
    check_tls_dsn();
    return tls_dsn.env;
}

__inline /*static*/ nfs_node *task::get_current_nfs()
{
    check_tls_dsn();
    return tls_dsn.nfs;
}

__inline /*static*/ timer_service *task::get_current_tsvc()
{
    check_tls_dsn();
    return tls_dsn.tsvc;
}

} // end namespace
