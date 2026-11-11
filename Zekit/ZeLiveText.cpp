#include "pch.h"

#include <mutex>
#include <utility>

#include "include/ZeLiveText.h"
#include "include/Zekit.h"
#include "ZeFrameCollector.h"

namespace Kit {

    struct LiveText::Shared {
        std::mutex m;
        std::string pending;
        bool has = false;
        std::function<void(std::string)> apply;
        Window* win = nullptr;
        std::atomic<bool> alive{ true };
    };

    LiveText::LiveText()
        : Label(), shared_(std::make_shared<Shared>()) {}

    LiveText::LiveText(std::string initial)
        : Label(std::move(initial)), shared_(std::make_shared<Shared>()) {}

    LiveText::~LiveText() {
        detachTimer();
        if (shared_) {
            std::lock_guard<std::mutex> lock(shared_->m);
            shared_->alive.store(false);
            shared_->apply = nullptr;
            shared_->win = nullptr;
        }
    }

    void LiveText::SetPull(PullFn fn, int intervalMs) {
        pull_ = std::move(fn);
        intervalMs_ = intervalMs < 0 ? 0 : intervalMs;

        detachTimer();
        if (attached_) startTimer();

        if (pull_) Refresh();
    }

    void LiveText::Stop() {
        detachTimer();
        intervalMs_ = 0;
    }

    void LiveText::SetInterval(int intervalMs) {
        intervalMs_ = intervalMs < 0 ? 0 : intervalMs;
        detachTimer();
        if (attached_) startTimer();
    }

    void LiveText::Refresh() {
        if (!pull_) return;
        SetText(pull_());
    }

    void LiveText::tick() {
        if (!pull_ || intervalMs_ <= 0) return;
        std::string next = pull_();
        if (redrawOnlyOnChange_ && next == Text()) return;
        SetText(next);
    }

    void LiveText::detachTimer() {
        if (timerId_ == 0) return;
        if (Window* w = window()) w->ClearTimer(static_cast<Window::TimerId>(timerId_));
        timerId_ = 0;
    }

    void LiveText::ensureAttached() {
        if (attached_) return;
        Window* w = window();
        if (!w) return;

        attached_ = true;
        {
            std::lock_guard<std::mutex> lock(shared_->m);
            shared_->win = w;
            shared_->apply = [this](std::string t) { SetText(t); };
        }
        if (pull_ && intervalMs_ > 0) startTimer();
    }

    void LiveText::startTimer() {
        Window* w = window();
        if (!w || !pull_ || intervalMs_ <= 0) return;

        auto slot = shared_;
        timerId_ = static_cast<uint64_t>(w->SetInterval(intervalMs_, [this, slot] {
            if (!slot->alive.load(std::memory_order_relaxed)) return;
            tick();
        }));
    }

    void LiveText::Post(std::string text) {
        if (!shared_) return;

        Window* w = nullptr;
        {
            std::lock_guard<std::mutex> lock(shared_->m);
            shared_->pending = std::move(text);
            shared_->has = true;
            w = shared_->win;
            if (!w || !shared_->apply) return;
        }

        auto slot = shared_;
        w->Post([slot] {
            std::string body;
            std::function<void(std::string)> apply;
            {
                std::lock_guard<std::mutex> lock(slot->m);
                if (!slot->has) return;
                body = std::move(slot->pending);
                slot->has = false;
                apply = slot->apply;
            }
            if (apply) apply(std::move(body));
        });
    }

    void LiveText::paintContent(FrameCollector& fc) const {
        const_cast<LiveText*>(this)->ensureAttached();
        Label::paintContent(fc);
    }

}
