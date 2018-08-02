#include "event.h"
#include <chrono>
#include <atomic>
#include <poll.h>

namespace posix_quic {

void Event::EventTrigger::Wait(int timeout)
{
    std::unique_lock<std::mutex> lock(cvMtx);
    if (triggered) return ;
    if (timeout > 0) {
        cv.wait_for(lock, std::chrono::milliseconds(timeout));
    } else if (timeout == 0) {
        return ;
    } else {
        cv.wait(lock);
    }
}

void Event::EventTrigger::Trigger()
{
    std::unique_lock<std::mutex> lock(cvMtx);
    triggered = true;
    cv.notify_one();
}

int Event::StartWait(EventWaiter waiter, EventTrigger * trigger)
{
    std::unique_lock<std::mutex> lock(mtx_);
    if ((*waiter.events & POLLIN) && Readable())
        *waiter.revents |= POLLIN;
    if ((*waiter.events & POLLOUT) && Writable())
        *waiter.revents |= POLLOUT;
    if (Error())
        *waiter.revents |= POLLERR;

    waitings_[trigger] = waiter;
    return *waiter.revents;
}
void Event::Trigger(int event)
{
    std::unique_lock<std::mutex> lock(mtx_);
    for (auto & kv : waitings_) {
        EventTrigger * trigger = kv.first;
        EventWaiter & waiter = kv.second;
        switch (event) {
            case POLLIN:
                if (*waiter.events & POLLIN) {
                    __atomic_fetch_or(waiter.revents, POLLIN, std::memory_order_seq_cst);
                    trigger->Trigger();
                    DebugPrint(dbg_event, "fd = %d, trigger event = POLLIN. waiter.revents = %s", Fd(), PollEvent2Str(*waiter.revents));
                }
                break;

            case POLLOUT:
                if (*waiter.events & POLLOUT) {
                    __atomic_fetch_or(waiter.revents, POLLOUT, std::memory_order_seq_cst);
                    trigger->Trigger();
                    DebugPrint(dbg_event, "fd = %d, trigger event = POLLOUT. waiter.revents = %s", Fd(), PollEvent2Str(*waiter.revents));
                }
                break;

            case POLLERR:
                __atomic_fetch_or(waiter.revents, POLLERR, std::memory_order_seq_cst);
                trigger->Trigger();
                DebugPrint(dbg_event, "fd = %d, trigger event = POLLERR. waiter.revents = %s", Fd(), PollEvent2Str(*waiter.revents));
                break;

            default:
                break;
        }
    }
}

void Event::StopWait(EventTrigger * trigger)
{
    std::unique_lock<std::mutex> lock(mtx_);
    waitings_.erase(trigger);
}

void Event::SetReadable(bool b)
{
    DebugPrint(dbg_event, "fd = %d, SetReadable(%s)", Fd(), b ? "true" : "false");
    readable = b;
    Trigger(POLLIN);
}
void Event::SetWritable(bool b)
{
    DebugPrint(dbg_event, "fd = %d, SetWritable(%s)", Fd(), b ? "true" : "false");
    writable = b;
    Trigger(POLLOUT);
}
void Event::SetError(int err, int quicErr)
{
    DebugPrint(dbg_event, "fd = %d, SetError(err=%d, quicErr=%d) now-error=%d",
            Fd(), err, quicErr, error);
    if (error == 0) {
        error = err;
        quicErrorCode = (QuicErrorCode)quicErr;
        Trigger(POLLERR);
    }
}

} // namespace posix_quic
