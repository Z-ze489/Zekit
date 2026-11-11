
#include "pch.h"

#include "ZeRhiBackend.h"

#if !defined(_WIN32) && !defined(__linux__) && !defined(__ANDROID__) \
 && !defined(__OHOS__) && !defined(__APPLE__)

#include <iostream>

namespace Rhi {

    class UnsupportedBackend final : public IBackend {
    public:
        explicit UnsupportedBackend(Api api) : m_api(api) {}
        ~UnsupportedBackend() override = default;

        Api  api()     const override { return m_api; }
        bool IsReady() const override { return false; }

        bool Init(void*, int, int) override {
            static bool reported = false;
            if (!reported) {
                reported = true;
                std::cerr
                    << "[Zekit] render backend '" << ApiName(m_api)
                    << "' is not implemented on this platform yet.\n"
                       "        Only the message loop will run; nothing will be drawn.\n"
                       "        Next step: implement it in ZeRenderBackend.cpp "
                       "(see Rhi::CreateBackend).\n";
            }
            return false;
        }

        void Shutdown() override {}
        bool Resize(int, int) override { return false; }

        bool DrawFrame(const Kit::FrameCollector&, int, int, float, float, float) override {
            return false;
        }

        void AttachTextureSinks(void*, void*) override {}
        void DetachTextureSinks(void*, void*) override {}

    private:
        Api m_api;
    };

    IBackend* CreateBackend(Api api) {
        if (api == Api::Software) return CreateSoftwareBackend();
        return new UnsupportedBackend(api);
    }

    void DestroyBackend(IBackend* backend) {
        delete backend;
    }

}

#endif
