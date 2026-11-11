
#include "pch.h"

#include "ZeMobileWindow.h"

#include <chrono>
#include <cstring>
#include <iterator>

namespace Plat {

	struct MobileWindow::Impl {
		std::unique_ptr<IMobileBackend> backend;
		IWindowHost* host = nullptr;
		WindowDesc   desc{};

		bool closed = false;
		int  exitCode = 0;
		bool shown = false;

		static constexpr int kQueueCap = 8;
		Event queue[kQueueCap]{};
		int   qHead = 0, qTail = 0;

		bool m_redrawPending = false;
		unsigned long long wakeDeadlineMs = 0;

		bool primaryDown = false;
		uint32_t upIds[MobileEvent::kMaxPointers]{};
		int      upCount = 0;

		float saLeft = 0, saTop = 0, saRight = 0, saBottom = 0;

		static unsigned long long NowMs() {
			return static_cast<unsigned long long>(
				std::chrono::duration_cast<std::chrono::milliseconds>(
					std::chrono::steady_clock::now().time_since_epoch()).count());
		}

		void Push(const Event& e) {
			const int next = (qTail + 1) % kQueueCap;
			if (next == qHead) {
				qHead = (qHead + 1) % kQueueCap;
			}
			queue[qTail] = e;
			qTail = next;
		}
		bool Pop(Event& out) {
			if (qHead == qTail) return false;
			out = queue[qHead];
			qHead = (qHead + 1) % kQueueCap;
			return true;
		}
		bool QueueEmpty() const { return qHead == qTail; }
		void ClearQueue() { qHead = qTail = 0; }

		void Translate(const MobileEvent& me);

		void EmitSimple(EventKind k) {
			Event e; e.kind = k; Push(e);
		}
	};

	void MobileWindow::Impl::Translate(const MobileEvent& me) {
		switch (me.kind) {

		case MobileEventKind::TouchBegin:
		case MobileEventKind::TouchMove:
		case MobileEventKind::TouchEnd:
		case MobileEventKind::TouchCancel: {
			Event e;
			switch (me.kind) {
			case MobileEventKind::TouchBegin:  e.kind = EventKind::PointerDown;   break;
			case MobileEventKind::TouchMove:   e.kind = EventKind::PointerMove;   break;
			case MobileEventKind::TouchEnd:    e.kind = EventKind::PointerUp;     break;
			default:                           e.kind = EventKind::PointerCancel; break;
			}
			e.device = PointerDevice::Touch;
			e.pointerCount = me.pointerCount > Event::kMaxPointers
				? Event::kMaxPointers : me.pointerCount;
			for (uint32_t i = 0; i < e.pointerCount; ++i) {
				e.pointers[i].id = me.pointers[i].id;
				e.pointers[i].x = me.pointers[i].x;
				e.pointers[i].y = me.pointers[i].y;
			}
			const uint32_t ci = me.changedIndex < e.pointerCount ? me.changedIndex : 0;
			if (e.pointerCount > 0) {
				e.pointerId = e.pointers[ci].id;
				e.x = e.pointers[ci].x;
				e.y = e.pointers[ci].y;
			}
			const uint32_t upId = e.pointerCount > 0 ? e.pointers[ci].id : 0;
			switch (me.kind) {
			case MobileEventKind::TouchBegin:
				if (upCount < static_cast<int>(std::size(upIds)))
					upIds[upCount++] = upId;
				break;
			case MobileEventKind::TouchEnd:
				for (int i = 0; i < upCount; ++i) {
					if (upIds[i] == upId) {
						upIds[i] = upIds[--upCount];
						break;
					}
				}
				break;
			case MobileEventKind::TouchCancel:
				upCount = 0;
				break;
			default: break;
			}
			primaryDown = (upCount > 0);
			Push(e);
			break;
		}

		case MobileEventKind::SurfaceReady: {
			EmitSimple(EventKind::SurfaceChanged);
			Event e; e.kind = EventKind::Resize;
			e.width = me.width; e.height = me.height;
			Push(e);
			break;
		}
		case MobileEventKind::SurfaceLost:
			EmitSimple(EventKind::SurfaceChanged);
			break;

		case MobileEventKind::Resized: {
			Event e; e.kind = EventKind::Resize;
			e.width = me.width; e.height = me.height;
			Push(e);
			backend->GetSafeAreaInsets(saLeft, saTop, saRight, saBottom);
			break;
		}
		case MobileEventKind::ConfigChanged:
			backend->GetSafeAreaInsets(saLeft, saTop, saRight, saBottom);
			EmitSimple(EventKind::ThemeChanged);
			break;

		case MobileEventKind::SoftKeyboardShown:
		case MobileEventKind::SoftKeyboardHidden: {
			Event e; e.kind = EventKind::SoftKeyboard;
			e.height = (me.kind == MobileEventKind::SoftKeyboardShown)
				? me.keyboardHeight : 0;
			Push(e);
			break;
		}

		case MobileEventKind::AppResumed:
		case MobileEventKind::AppPaused:
		case MobileEventKind::AppLowMemory:
		case MobileEventKind::AppTerminating: {
			Event e; e.kind = EventKind::AppLifecycleEvent;
			switch (me.kind) {
			case MobileEventKind::AppResumed:      e.lifecycle = AppLifecycle::Resumed;     break;
			case MobileEventKind::AppPaused:       e.lifecycle = AppLifecycle::Paused;      break;
			case MobileEventKind::AppLowMemory:    e.lifecycle = AppLifecycle::LowMemory;   break;
			default:                               e.lifecycle = AppLifecycle::Terminating; break;
			}
			Push(e);
			break;
		}

		case MobileEventKind::FocusGained: EmitSimple(EventKind::FocusIn);  break;
		case MobileEventKind::FocusLost:   EmitSimple(EventKind::FocusOut); break;

		case MobileEventKind::KeyDown:
		case MobileEventKind::KeyUp: {
			Event e;
			e.kind = (me.kind == MobileEventKind::KeyDown)
				? EventKind::KeyDown : EventKind::KeyUp;
			e.key = me.key;
			e.virtualKey = me.rawKeyCode;
			e.isRepeat = me.isRepeat;
			e.mods = me.mods;
			Push(e);

			if (me.kind == MobileEventKind::KeyDown && me.unicodeChar >= 32) {
				Event c;
				c.kind = EventKind::CharInput;
				c.codepoint = me.unicodeChar;
				c.mods = me.mods;
				Push(c);
			}
			break;
		}

		case MobileEventKind::TextInput: {
			Event c;
			c.kind = EventKind::CharInput;
			c.codepoint = me.unicodeChar;
			c.mods = me.mods;
			Push(c);
			break;
		}

		case MobileEventKind::QuitRequested:
			exitCode = 0;
			EmitSimple(EventKind::Quit);
			break;

		default: break;
		}
	}

	MobileWindow::MobileWindow(IMobileBackend* backend)
		: m_impl(std::make_unique<Impl>()) {
		m_impl->backend.reset(backend);
	}

	MobileWindow::~MobileWindow() {
		if (m_impl) Destroy();
	}

	Cap MobileWindow::Capabilities() const {
		return m_impl && m_impl->backend ? m_impl->backend->MobileCapabilities()
			: Cap::None;
	}

	bool MobileWindow::Create(const WindowDesc& desc, IWindowHost& host) {
		if (!m_impl || !m_impl->backend) return false;

		m_impl->desc = desc;
		m_impl->host = &host;

		m_impl->backend->GetSafeAreaInsets(m_impl->saLeft, m_impl->saTop,
			m_impl->saRight, m_impl->saBottom);

		m_impl->backend->SetImmersive(false);

		return true;
	}

	bool MobileWindow::StartsImmersive() const { return false; }

	void MobileWindow::Show() {
		if (!m_impl || m_impl->closed) return;
		m_impl->shown = true;
		RequestRedraw();
	}

	void MobileWindow::Hide() {
	}

	void MobileWindow::Destroy() {
		if (!m_impl || m_impl->closed) return;

		m_impl->ClearQueue();
		m_impl->closed = true;

		if (m_impl->host) {
			IWindowHost* h = m_impl->host;
			m_impl->host = nullptr;
			h->OnNativeClosed();
		}
	}

	bool MobileWindow::PollEvent() {
		if (!m_impl || m_impl->closed || !m_impl->backend) return false;

		for (;;) {
			Event e;
			if (m_impl->Pop(e)) {
				if (m_impl->host) m_impl->host->OnPlatformEvent(e);
				return true;
			}

			if (m_impl->m_redrawPending) {
				m_impl->m_redrawPending = false;
				Event r; r.kind = EventKind::Redraw;
				if (m_impl->host) m_impl->host->OnPlatformEvent(r);
				return true;
			}

			MobileEvent me;
			if (!m_impl->backend->PollMobileEvent(me)) return false;
			m_impl->Translate(me);
		}
	}

	void MobileWindow::WaitEvents(int timeoutMs) {
		if (!m_impl || m_impl->closed || !m_impl->backend) return;

		int effective = timeoutMs;
		if (m_impl->wakeDeadlineMs != 0) {
			const unsigned long long now = Impl::NowMs();
			if (m_impl->wakeDeadlineMs <= now) {
				m_impl->wakeDeadlineMs = 0;
				m_impl->m_redrawPending = true;
				effective = 0;
			} else {
				const int remain = static_cast<int>(m_impl->wakeDeadlineMs - now);
				if (effective < 0 || remain < effective) effective = remain;
			}
		}
		if (m_impl->m_redrawPending || !m_impl->QueueEmpty()) effective = 0;

		m_impl->backend->WaitMobileEvents(effective);

		if (m_impl->wakeDeadlineMs != 0 && m_impl->wakeDeadlineMs <= Impl::NowMs()) {
			m_impl->wakeDeadlineMs = 0;
			m_impl->m_redrawPending = true;
		}
	}

	int MobileWindow::ExitCode() const { return m_impl ? m_impl->exitCode : 0; }

	void MobileWindow::GetClientSize(int& w, int& h) const {
		w = h = 0;
		if (!m_impl || !m_impl->backend) return;
		void* surf = nullptr;
		int pw = 0, ph = 0;
		if (!m_impl->backend->QuerySurface(surf, pw, ph)) return;
		const float d = m_impl->backend->Density();
		const float scale = (d > 0.0f) ? d : 1.0f;
		w = static_cast<int>(static_cast<float>(pw) / scale + 0.5f);
		h = static_cast<int>(static_cast<float>(ph) / scale + 0.5f);
	}

	void MobileWindow::SetClientSize(int, int) {
	}

	void MobileWindow::SetPosition(int, int) {
	}

	void MobileWindow::CenterOnScreen() {
	}

	void MobileWindow::SetCenterOnResize(bool) {
	}

	void MobileWindow::SetSizeLimits(int, int, int, int) {
	}

	float MobileWindow::ScaleFactor() const {
		return m_impl && m_impl->backend ? m_impl->backend->Density() : 1.0f;
	}

	int MobileWindow::Dpi() const {
		const float d = ScaleFactor();
		return static_cast<int>(d * 96.0f + 0.5f);
	}

	float MobileWindow::RenderScale() const {
		return m_impl && m_impl->backend ? m_impl->backend->Density() : 1.0f;
	}

	void MobileWindow::Minimize() {
		if (m_impl && m_impl->backend) m_impl->backend->RequestAppMoveToBack();
	}

	void MobileWindow::Maximize() {
		if (m_impl && m_impl->backend) m_impl->backend->SetImmersive(true);
	}

	void MobileWindow::Restore() {
		if (m_impl && m_impl->backend) m_impl->backend->SetImmersive(false);
	}

	bool MobileWindow::IsMinimized() const {
		return false;
	}

	bool MobileWindow::IsMaximized() const {
		return m_impl && m_impl->backend && m_impl->backend->IsImmersive();
	}

	void MobileWindow::SetTopmost(bool) {
	}

	void MobileWindow::SetResizable(bool) {
	}

	void MobileWindow::SetFullscreen(bool on) {
		if (m_impl && m_impl->backend) m_impl->backend->SetImmersive(on);
	}

	void MobileWindow::SetBorderless(bool) {
	}

	void MobileWindow::ApplyTitleBar(bool, bool, bool, bool) {
	}

	void MobileWindow::SetIconFromFile(const std::string&) {
	}
	void MobileWindow::SetIconFromResource(int) {
	}
	void MobileWindow::SetIconFromHandle(void*) {
	}

	void MobileWindow::SetOpacity(float alpha) {
		if (m_impl && m_impl->backend) m_impl->backend->SetWindowAlpha(alpha);
	}

	void MobileWindow::SetDarkTitleBar(bool dark) {
		if (m_impl && m_impl->backend) m_impl->backend->SetDarkSystemBar(dark);
	}

	void MobileWindow::SetBackdropColor(unsigned int rgb) {
		if (m_impl && m_impl->backend) m_impl->backend->SetWindowBackground(rgb);
	}

	std::string MobileWindow::GetClipboardText() {
		if (m_impl && m_impl->backend) return m_impl->backend->GetClipboardText();
		return std::string();
	}
	void MobileWindow::SetClipboardText(const std::string& utf8) {
		if (m_impl && m_impl->backend) m_impl->backend->SetClipboardText(utf8);
	}

	void MobileWindow::SetImeSpot(float, float, float) {
	}

	void MobileWindow::ShowSoftKeyboard(bool on) {
		if (m_impl && m_impl->backend) m_impl->backend->ShowSoftKeyboard(on);
	}

	bool MobileWindow::IsSoftKeyboardVisible() const {
		return m_impl && m_impl->backend && m_impl->backend->IsSoftKeyboardVisible();
	}

	void MobileWindow::GetSafeAreaInsets(float& left, float& top,
		float& right, float& bottom) const {
		if (!m_impl) { left = top = right = bottom = 0.0f; return; }
		left = m_impl->saLeft;  top = m_impl->saTop;
		right = m_impl->saRight; bottom = m_impl->saBottom;
	}

	void MobileWindow::SetTitle(const std::string& utf8) {
		if (m_impl && m_impl->backend) m_impl->backend->SetActivityTitle(utf8);
	}

	bool MobileWindow::IsClosed() const { return !m_impl || m_impl->closed; }

	bool MobileWindow::IsActive() const {
		return m_impl && m_impl->backend && m_impl->backend->HasFocus();
	}

	void MobileWindow::RequestRedraw() {
		if (!m_impl || m_impl->closed || !m_impl->backend) return;
		m_impl->m_redrawPending = true;
		m_impl->backend->WakeEventLoop();
	}

	void MobileWindow::RequestWakeup(int afterMs) {
		if (!m_impl) return;
		const unsigned long long delay = afterMs > 0
			? static_cast<unsigned long long>(afterMs) : 1ull;
		const unsigned long long deadline = Impl::NowMs() + delay;

		if (m_impl->wakeDeadlineMs != 0 && deadline >= m_impl->wakeDeadlineMs)
			return;
		m_impl->wakeDeadlineMs = deadline;
		if (m_impl->backend) m_impl->backend->WakeEventLoop();
	}

	void MobileWindow::CancelWakeup() {
		if (!m_impl) return;
		m_impl->wakeDeadlineMs = 0;
	}

	void MobileWindow::SetTimerPrecision(bool ) {
	}

	void MobileWindow::PostClose() {
		if (m_impl && m_impl->backend) m_impl->backend->RequestAppQuit();
	}

	void MobileWindow::RequestQuit(int exitCode) {
		if (!m_impl) return;
		m_impl->exitCode = exitCode;
		Event e; e.kind = EventKind::Quit;
		m_impl->Push(e);
		if (m_impl->backend) m_impl->backend->WakeEventLoop();
	}

	void MobileWindow::StartWindowDrag() {
	}

	bool MobileWindow::IsMouseButtonDown(MouseButton b) const {
		if (b != MouseButton::Left || !m_impl) return false;
		return m_impl->backend && m_impl->backend->HasFocus() && m_impl->primaryDown;
	}

	void* MobileWindow::NativeHandle() const {
		if (!m_impl || !m_impl->backend) return nullptr;
		void* surf = nullptr;
		int w = 0, h = 0;
		if (!m_impl->backend->QuerySurface(surf, w, h)) return nullptr;
		return surf;
	}

}
