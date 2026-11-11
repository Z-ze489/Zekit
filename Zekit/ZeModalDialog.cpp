
#include "pch.h"

#include <algorithm>
#include <string>
#include <utility>

#include "include/ZeModalDialog.h"
#include "include/Zekit.h"

#include "ZeFrameCollector.h"
#include "ZeDrawUtil.h"

#ifndef NOMINMAX
#  define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#  define WIN32_LEAN_AND_MEAN
#endif
#if defined(_WIN32)
#  include <windows.h>
#endif

namespace Kit {

    namespace {
        constexpr const char* kSessionName = "\xC2\xA7""ModalSession";
    }

    class ModalSessionWidget : public Widget {
    public:
        class KeyFilter : public EventFilter {
        public:
            explicit KeyFilter(ModalSessionWidget* owner) : owner_(owner) {}

            bool onKey(Window& win, KeyEvent& e) override {
                if (e.type != EventType::KeyPress) return false;
                if (!owner_ || !owner_->active_) return false;

                detail::ModalState* s = owner_->state_.get();
                if (!s) return false;

                switch (e.key) {
                case Key::Escape:
                    win.Close();
                    e.accept();
                    return true;

                case Key::Enter:
                    if (!s->defaultButtonName.empty()) {
                        if (s->onAccepted && s->content) s->onAccepted(*s->content);
                        s->forceClose = true;
                        win.Close();
                        e.accept();
                        return true;
                    }
                    return false;

                default:
                    return false;
                }
            }

        private:
            ModalSessionWidget* owner_ = nullptr;
        };

        ModalSessionWidget() { SetVisible(false); }

        void activate(Window& host, Window& dialog,
                      std::shared_ptr<detail::ModalState> state) {
            host_ = &host;
            state_ = std::move(state);
            active_ = true;
            SetVisible(true);

            dialog_ = &dialog;
            filter_ = std::make_unique<KeyFilter>(this);
            dialog.InstallEventFilter(*filter_);

            prevCloseAsk_ = host.OnCloseRequest();
            host.SetOnCloseRequest([this](Window& w) -> bool {
                if (active_) {
#if defined(_WIN32)
                    ::MessageBeep(MB_ICONEXCLAMATION);
#else
                    (void)w;
#endif
                    return false;
                }
                return prevCloseAsk_ ? prevCloseAsk_(w) : true;
            });

            savedMin_   = host.MinimizeButtonEnabled();
            savedMax_   = host.MaximizeButtonEnabled();
            savedClose_ = host.CloseButtonEnabled();
            host.SetMinimizeButtonEnabled(false);
            host.SetMaximizeButtonEnabled(false);
            host.SetCloseButtonEnabled(false);
        }

        void dismiss() {
            if (!active_) return;
            active_ = false;
            SetVisible(false);
            dialog_ = nullptr;
            filter_.reset();
            if (host_ && !host_->IsClosed()) {
                host_->SetOnCloseRequest(std::move(prevCloseAsk_));
                host_->SetMinimizeButtonEnabled(savedMin_);
                host_->SetMaximizeButtonEnabled(savedMax_);
                host_->SetCloseButtonEnabled(savedClose_);
            }
            prevCloseAsk_ = nullptr;
            state_.reset();
        }

        bool isActive() const { return active_; }

        bool paintsOnTop() const override { return active_; }
        bool HitTest(float x, float y) const override {
            if (!active_) return false;
            if (const Window* w = window()) {
                return x >= 0.0f && y >= 0.0f
                    && x < static_cast<float>(const_cast<Window*>(w)->width())
                    && y < static_cast<float>(const_cast<Window*>(w)->height());
            }
            return false;
        }

        void Paint(FrameCollector& fc) const override {
            if (!active_) return;

            const Window* w = window();
            if (!w) return;
            const float winW = static_cast<float>(const_cast<Window*>(w)->width());
            const float winH = static_cast<float>(const_cast<Window*>(w)->height());
            if (winW <= 0.0f || winH <= 0.0f) return;

            bool draw = true;
            Color4f color{ 0.0f, 0.0f, 0.0f, 0.0f };
            if (state_) {
                draw = state_->scrimEnabled;
                color = state_->scrimColor;
            }
            if (!draw) return;

            if (color.a <= 0.0f) color = fc.GetTheme().modalDialog.scrimColor;
            if (color.a <= 0.0f) return;

            fc.ResetClip();
            drawutil::PaintRoundedRect(fc, 0.0f, 0.0f, winW, winH, 0.0f, color);
        }

    private:
        Window* host_ = nullptr;
        Window* dialog_ = nullptr;
        std::shared_ptr<detail::ModalState> state_;
        bool    active_ = false;
        std::unique_ptr<KeyFilter> filter_;
        std::function<bool(Window&)> prevCloseAsk_;
        bool savedMin_ = true, savedMax_ = true, savedClose_ = true;
    };

    namespace {
        ModalSessionWidget* findSession(Window& host) {
            Widget* w = host.FindWidget<Widget>(kSessionName);
            return static_cast<ModalSessionWidget*>(w);
        }

        ModalSessionWidget* ensureSession(Window& host) {
            if (ModalSessionWidget* s = findSession(host)) return s;
            ModalSessionWidget sess;
            sess.SetObjectName(kSessionName);
            ModalSessionWidget& ref = host.AddWidget(std::move(sess));
            return &ref;
        }

        void EmitSound(const std::shared_ptr<detail::ModalState>& st) {
            if (!st || !st->soundEnabled) return;
            if (st->soundProfile == SoundProfile::None) return;

            if (st->onSoundRequest && st->onSoundRequest(st->soundProfile)) return;

#if defined(_WIN32)
            UINT type = MB_OK;
            switch (st->soundProfile) {
            case SoundProfile::Info:     type = MB_ICONASTERISK; break;
            case SoundProfile::Question: type = MB_ICONQUESTION; break;
            case SoundProfile::Warning:  type = MB_ICONWARNING;  break;
            case SoundProfile::Error:    type = MB_ICONHAND;     break;
            case SoundProfile::Success:  type = MB_ICONASTERISK; break;
            case SoundProfile::None:     return;
            }
            if (!st->soundFile.empty()) type = MB_OK;
            ::MessageBeep(type);
#endif
        }
    }

    ModalDialog ModalDialog::Show(Window& host) {
        return Show(host, Options());
    }

    ModalDialog ModalDialog::Show(Window& host, Options opt) {
        ModalDialog dlg;
        dlg.m_state = std::make_shared<detail::ModalState>();

        auto& st = *dlg.m_state;
        st.host = &host;
        st.title = opt.title;
        st.scrimEnabled = opt.scrim;
        st.soundEnabled = opt.sound;
        st.soundProfile = opt.soundProfile;

        const int w = static_cast<int>(std::max(opt.width, 120.0f));
        const int h = static_cast<int>(std::max(opt.height, 60.0f));
        Window& win = host.CreateChildWindow(0, 0, w, h, ChildKind::Owned);
        if (win.IsClosed()) {
            st.win = nullptr;
            return dlg;
        }
        st.win = &win;

        win.SetTitle(opt.title);
        win.SetResizable(opt.resizable);

        Panel content;
        content.SetPadding(16.0f, 14.0f);
        content.SetGap(10.0f, 10.0f);
        Panel& c = win.AddWidget(std::move(content));
        st.content = &c;

        win.CenterInHost();

        ModalSessionWidget* sess = ensureSession(host);
        sess->activate(host, win, dlg.m_state);

        Window* hostPtr = &host;
        std::weak_ptr<detail::ModalState> wst = dlg.m_state;
        win.SetOnClosed([hostPtr, wst](Window&) {
            auto s = wst.lock();

            hostPtr->SetEnabled(true);
            if (ModalSessionWidget* sess = findSession(*hostPtr)) sess->dismiss();

            if (s && s->onClosed && s->content) s->onClosed(*s->content);
        });

        win.SetOnCloseRequest([wst](Window&) -> bool {
            auto s = wst.lock();
            if (!s) return true;
            if (s->forceClose) return true;
            if (!s->onRequestClose) return true;
            if (s->content) return s->onRequestClose(*s->content);
            return true;
        });

        host.SetEnabled(false);

        EmitSound(dlg.m_state);

        return dlg;
    }

    void ModalDialog::SetTitle(std::string title) {
        if (!m_state || !m_state->win) return;
        m_state->title = std::move(title);
        m_state->win->SetTitle(m_state->title);
    }

    const std::string& ModalDialog::title() const {
        static const std::string kEmpty;
        return m_state ? m_state->title : kEmpty;
    }

    void ModalDialog::SetWindowSize(float w, float h) {
        if (!m_state || !m_state->win) return;
        m_state->win->SetSize(static_cast<int>(std::max(w, 120.0f)),
                              static_cast<int>(std::max(h, 60.0f)));
    }

    float ModalDialog::windowWidth() const {
        if (!m_state || !m_state->win) return 0.0f;
        int w = 0, h = 0;
        m_state->win->GetClientSize(w, h);
        return static_cast<float>(w);
    }

    float ModalDialog::windowHeight() const {
        if (!m_state || !m_state->win) return 0.0f;
        int w = 0, h = 0;
        m_state->win->GetClientSize(w, h);
        return static_cast<float>(h);
    }

    void ModalDialog::CenterInHost() {
        if (!m_state || !m_state->win) return;
        m_state->win->CenterInHost();
    }

    void ModalDialog::SetResizable(bool on) {
        if (!m_state || !m_state->win) return;
        m_state->win->SetResizable(on);
    }

    bool ModalDialog::isOpen() const {
        return m_state && m_state->win && !m_state->win->IsClosed();
    }

    void ModalDialog::Close() {
        if (!m_state || !m_state->win) return;
        m_state->win->Close();
    }

    void ModalDialog::Accept() {
        if (!m_state) return;
        if (m_state->onAccepted && m_state->content) m_state->onAccepted(*m_state->content);
        m_state->forceClose = true;
        Close();
    }

    void ModalDialog::Reject() {
        if (!m_state || !m_state->win) return;
        Close();
    }

    void ModalDialog::SetClosable(bool on) {
        if (!m_state || !m_state->win) return;
        m_state->closable = on;
        m_state->win->SetCloseButtonEnabled(on);
    }

    bool ModalDialog::isClosable() const {
        return m_state ? m_state->closable : false;
    }

    void ModalDialog::SetScrimEnabled(bool on) {
        if (!m_state) return;
        m_state->scrimEnabled = on;
        if (m_state->host) m_state->host->RequestRedraw();
    }

    void ModalDialog::SetScrimColor(const Color4f& c) {
        if (!m_state) return;
        m_state->scrimColor = c;
        if (m_state->host) m_state->host->RequestRedraw();
    }

    bool ModalDialog::scrimEnabled() const {
        return m_state ? m_state->scrimEnabled : false;
    }

    void ModalDialog::SetSoundEnabled(bool on) { if (m_state) m_state->soundEnabled = on; }

    bool ModalDialog::soundEnabled() const { return m_state ? m_state->soundEnabled : false; }

    void ModalDialog::SetSoundProfile(SoundProfile p) { if (m_state) m_state->soundProfile = p; }

    SoundProfile ModalDialog::soundProfile() const {
        return m_state ? m_state->soundProfile : SoundProfile::Warning;
    }

    void ModalDialog::PlaySound() { EmitSound(m_state); }

    void ModalDialog::SetOnSoundRequest(std::function<bool(SoundProfile)> cb) {
        if (m_state) m_state->onSoundRequest = std::move(cb);
    }

    void ModalDialog::SetOnSoundFile(std::string path) {
        if (m_state) m_state->soundFile = std::move(path);
    }

    void ModalDialog::SetDefaultButtonName(std::string name) {
        if (m_state) m_state->defaultButtonName = std::move(name);
    }

    void ModalDialog::SetCancelButtonName(std::string name) {
        if (m_state) m_state->cancelButtonName = std::move(name);
    }

    void ModalDialog::SetOnClosed(SelfAction cb) {
        if (m_state) m_state->onClosed = std::move(cb);
    }

    void ModalDialog::SetOnAccepted(SelfAction cb) {
        if (m_state) m_state->onAccepted = std::move(cb);
    }

    void ModalDialog::SetOnRequestClose(std::function<bool(Widget&)> cb) {
        if (m_state) m_state->onRequestClose = std::move(cb);
    }

    Window* ModalDialog::nativeWindow() const {
        return m_state ? m_state->win : nullptr;
    }

    Widget* ModalDialog::sender() const {
        return m_state ? static_cast<Widget*>(m_state->content) : nullptr;
    }

}
