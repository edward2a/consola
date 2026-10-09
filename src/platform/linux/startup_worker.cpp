// SPDX-License-Identifier: MIT

#include <platform/startup.hpp>

#include <cstdint>
#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_set>

#include <sys/eventfd.h>
#include <unistd.h>

#include "pty_session.hpp"

namespace consola {
namespace {

constexpr std::size_t kMaxRequests = 64;
constexpr std::size_t kMaxResults = 64;

} // namespace

struct StartupWorker::Impl {
    Impl(WakeCallback wake_callback, void *context)
        : wake(wake_callback), wake_context(context), thread([this] { run(); })
    {}

    ~Impl()
    {
        stop();
    }

    void run();
    void process(const LaunchRequest &request);
    void push_result(StartupResult result);
    void wake_ui() const;
    void stop();

    WakeCallback wake;
    void *wake_context;

    std::mutex mutex;
    std::condition_variable request_cv;
    std::condition_variable result_cv;
    std::deque<LaunchRequest> requests;
    std::deque<StartupResult> results;
    std::unordered_set<std::uint64_t> cancelled;
    bool stopping = false;
    bool stopped = false;
    std::thread thread;

    std::mutex cancel_mutex;
    std::uint64_t current_id = 0;
    int current_cancel_fd = -1;
};

void StartupWorker::Impl::run()
{
    for (;;) {
        LaunchRequest request;
        {
            std::unique_lock lock(mutex);
            request_cv.wait(lock, [this] { return stopping || !requests.empty(); });
            if (stopping) {
                return;
            }
            request = std::move(requests.front());
            requests.pop_front();
        }
        process(request);
    }
}

void StartupWorker::Impl::process(const LaunchRequest &request)
{
    int cancel_fd = ::eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
    {
        std::scoped_lock lock(cancel_mutex);
        current_id = request.id.value;
        current_cancel_fd = cancel_fd;
    }

    bool queued_cancelled = false;
    {
        std::scoped_lock lock(mutex);
        queued_cancelled = cancelled.erase(request.id.value) > 0;
    }
    if (queued_cancelled) {
        {
            std::scoped_lock lock(cancel_mutex);
            current_id = 0;
            current_cancel_fd = -1;
        }
        if (cancel_fd >= 0) {
            ::close(cancel_fd);
        }
        StartupResult result;
        result.id = request.id;
        result.error = SessionError::cancelled;
        push_result(std::move(result));
        return;
    }

    auto session = std::make_unique<LinuxPtySession>(cancel_fd);
    SessionError error = session->start(request.options);

    {
        std::scoped_lock lock(cancel_mutex);
        current_id = 0;
        current_cancel_fd = -1;
    }
    if (cancel_fd >= 0) {
        ::close(cancel_fd);
    }

    {
        std::scoped_lock lock(mutex);
        cancelled.erase(request.id.value);
    }

    StartupResult result;
    result.id = request.id;
    result.error = error;
    if (error == SessionError::none) {
        result.master_fd = session->master_fd();
        result.exit_notification_fd = session->exit_notification_fd();
        result.process_id = session->process_id();
        result.session = std::move(session);
    }
    push_result(std::move(result));
}

void StartupWorker::Impl::push_result(StartupResult result)
{
    {
        std::unique_lock lock(mutex);
        result_cv.wait(lock, [this] { return results.size() < kMaxResults || stopping; });
        results.push_back(std::move(result));
    }
    wake_ui();
}

void StartupWorker::Impl::wake_ui() const
{
    if (wake != nullptr) {
        wake(wake_context);
    }
}

void StartupWorker::Impl::stop()
{
    {
        std::scoped_lock lock(mutex);
        if (stopped) {
            return;
        }
        stopped = true;
        stopping = true;
    }
    request_cv.notify_all();
    result_cv.notify_all();
    {
        std::scoped_lock lock(cancel_mutex);
        if (current_id != 0 && current_cancel_fd >= 0) {
            std::uint64_t one = 1;
            ssize_t ignored = ::write(current_cancel_fd, &one, sizeof(one));
            static_cast<void>(ignored);
        }
    }
    if (thread.joinable()) {
        thread.join();
    }
    {
        // Resolve every request still queued at stop. This may temporarily
        // exceed the soft result bound (by at most the request bound); the
        // worker is gone and the caller drains the results.
        std::scoped_lock lock(mutex);
        while (!requests.empty()) {
            StartupResult result;
            result.id = requests.front().id;
            result.error = SessionError::cancelled;
            requests.pop_front();
            results.push_back(std::move(result));
        }
    }
    result_cv.notify_all();
}

StartupWorker::StartupWorker(WakeCallback wake, void *wake_context)
    : impl_(std::make_unique<Impl>(wake, wake_context))
{}

StartupWorker::~StartupWorker() = default;

bool StartupWorker::submit(LaunchRequest request)
{
    std::scoped_lock lock(impl_->mutex);
    if (impl_->stopping || impl_->requests.size() >= kMaxRequests) {
        return false;
    }
    impl_->requests.push_back(std::move(request));
    impl_->request_cv.notify_one();
    return true;
}

bool StartupWorker::try_pop(StartupResult &out)
{
    std::scoped_lock lock(impl_->mutex);
    if (impl_->results.empty()) {
        return false;
    }
    out = std::move(impl_->results.front());
    impl_->results.pop_front();
    impl_->result_cv.notify_one();
    return true;
}

void StartupWorker::cancel(TerminalId id)
{
    {
        std::scoped_lock lock(impl_->mutex);
        impl_->cancelled.insert(id.value);
    }
    {
        std::scoped_lock lock(impl_->cancel_mutex);
        if (impl_->current_id == id.value && impl_->current_cancel_fd >= 0) {
            std::uint64_t one = 1;
            ssize_t ignored = ::write(impl_->current_cancel_fd, &one, sizeof(one));
            static_cast<void>(ignored);
        }
    }
}

void StartupWorker::stop()
{
    impl_->stop();
}

} // namespace consola
