#include "diag/ffi_guard.hpp"
#include "diag/black_box.hpp"

namespace ecotiter::diag {

static uint8_t getThreadId() noexcept {
    return 0;
}

FfiGuard::FfiGuard(uint16_t boundaryId) noexcept
    : boundaryId_(boundaryId) {
    auto& bb = BlackBox::instance();
    BlackBox::Event ev = {};
    ev.type = BlackBox::EventType::FfiEnter;
    ev.threadId = getThreadId();
    ev.payloadId = boundaryId_;
    ev.payloadValue = 0;
    bb.record(ev);
}

FfiGuard::~FfiGuard() noexcept {
    if (!exited_) {
        auto& bb = BlackBox::instance();
        BlackBox::Event ev = {};
        ev.type = BlackBox::EventType::FfiExit;
        ev.threadId = getThreadId();
        ev.payloadId = boundaryId_;
        ev.payloadValue = 0;
        bb.record(ev);
    }
}

} // namespace ecotiter::diag
