#include "pch.h"
#include "ZeFontRenderer.h"
#include "ZeFreeTypeWrapper.h"

#include <algorithm>
#include <cmath>
#include <iostream>

namespace Kit {

    namespace {
        constexpr unsigned int kDefaultPixelSize = 20;

        constexpr std::size_t kMaxQueuedTasks = 2048;

        constexpr int kInlinePumpBudget = 8;

        inline bool IsLayoutControl(char32_t cp) {
            return cp < 0x20 || cp == 0x7F || (cp >= 0x80 && cp <= 0x9F);
        }

        uint32_t CreateGlyphTexture(ITextureSink* sink, const ftw::GlyphBitmap& bmp, int pitch)
        {
            if (!sink) return 0;
            return bmp.isColor
                ? sink->CreateRGBA(bmp.width, bmp.height, bmp.buffer.data(), pitch, true)
                : sink->CreateGray(bmp.width, bmp.height, bmp.buffer.data(), pitch);
        }

        void DestroyGlyphTexture(ITextureSink* sink, uint32_t& tex)
        {
            if (!tex) return;
            if (sink) sink->Destroy(tex);
            tex = 0;
        }

        bool EnsureFacesAtSize(std::vector<std::unique_ptr<ftw::FontFace>>& faces,
            std::vector<unsigned int>& sizes,
            unsigned int px,
            unsigned int& confirmed)
        {
            if (faces.empty() || px == 0) return false;
            if (px == confirmed) return true;
            if (sizes.size() != faces.size()) sizes.assign(faces.size(), 0u);

            bool allOk = true;
            for (std::size_t i = 0; i < faces.size(); ++i) {
                if (sizes[i] == px) continue;
                try {
                    faces[i]->setPixelSize(px);
                    sizes[i] = px;
                }
                catch (const std::exception&) {
                    sizes[i] = 0;
                    allOk = false;
                }
            }
            if (allOk) confirmed = px;
            return allOk;
        }

        bool RasterizeFrom(std::vector<std::unique_ptr<ftw::FontFace>>& faces,
            char32_t cp, ftw::GlyphBitmap& out)
        {
            for (auto& f : faces) {
                if (!f->hasGlyph(cp)) continue;
                try {
                    const ftw::GlyphBitmap& bmp = f->loadGlyph(cp);
                    out = bmp;
                    return true;
                }
                catch (const std::exception&) {
                    return false;
                }
            }
            return false;
        }
    }

    FontRenderer::FontRenderer() = default;

    FontRenderer::~FontRenderer() { Shutdown(); }

    bool FontRenderer::Init() {
        if (!faces_.empty()) return true;

        const unsigned int defPx = PhysicalSizeOf(kDefaultPixelSize);

        faces_ = ftw::createDefaultFontFaces(defPx);
        if (faces_.empty()) {
            std::cerr << "[FontRenderer] no usable font found.\n";
            return false;
        }

        pixelSize_ = kDefaultPixelSize;
        faceSize_ = defPx;
        faceSizes_.assign(faces_.size(), defPx);

        StartWorker();

        return true;
    }

    void FontRenderer::SetScale(float scale) {
        if (!(scale > 0.0f) || !std::isfinite(scale)) scale = 1.0f;
        if (std::fabs(scale - scale_) < 1e-4f) return;

        scale_ = scale;

        DropAllGlyphTextures();
        faceSize_ = 0;
        faceSizes_.assign(faces_.size(), 0u);
    }

    void FontRenderer::Shutdown() {
        StopWorker();
        DropAllGlyphTextures();
        faces_.clear();
        faceSizes_.clear();
        faceSize_ = 0;
        pixelSize_ = 0;
    }

    void FontRenderer::DropAllGlyphTextures() {
        epoch_.fetch_add(1, std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lk(mtx_);
            tasks_.clear();
            done_.clear();
        }
        queued_.store(0, std::memory_order_relaxed);

        for (auto& [px, bucket] : caches_)
            for (auto& [cp, g] : bucket.glyphs)
                DestroyGlyphTexture(sink_, g.tex);
        caches_.clear();
        ++contentEpoch_;
        ++dropEpoch_;
    }

    void FontRenderer::EnsureFaceSize(unsigned int px) {
        EnsureFacesAtSize(faces_, faceSizes_, px, faceSize_);
    }

    void FontRenderer::StartWorker() {
        if (worker_.joinable()) return;

        std::lock_guard<std::mutex> lk(mtx_);
        tasks_.clear();
        done_.clear();
        stopping_.store(false, std::memory_order_relaxed);
        queued_.store(0, std::memory_order_relaxed);
        worker_ = std::thread(&FontRenderer::WorkerMain, this);
    }

    void FontRenderer::StopWorker() {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            stopping_.store(true, std::memory_order_relaxed);
        }
        cv_.notify_all();

        if (worker_.joinable()) {
            if (worker_.get_id() == std::this_thread::get_id()) {
                worker_.detach();
            }
            else {
                worker_.join();
            }
        }

        wake_ = nullptr;
        workerFaces_.clear();
        workerFaceSizes_.clear();
        workerLib_.reset();
        workerFacePx_ = 0;

        std::lock_guard<std::mutex> lk(mtx_);
        tasks_.clear();
        done_.clear();
        queued_.store(0, std::memory_order_relaxed);
    }

    void FontRenderer::SetWakeCallback(std::function<void()> cb) {
        std::lock_guard<std::mutex> lk(mtx_);
        wake_ = std::move(cb);
    }

    void FontRenderer::WorkerMain() {
        for (;;) {
            std::deque<GlyphTask> batch;
            {
                std::unique_lock<std::mutex> lk(mtx_);
                cv_.wait(lk, [this] {
                    return stopping_.load(std::memory_order_relaxed) || !tasks_.empty();
                    });
                if (stopping_.load(std::memory_order_relaxed)) return;
                batch.swap(tasks_);
            }

            std::sort(batch.begin(), batch.end(),
                [](const GlyphTask& a, const GlyphTask& b) { return a.px < b.px; });

            if (workerFaces_.empty()) {
                try {
                    workerLib_ = ftw::Library::createIndependent();
                    workerFacePx_ = batch.empty() ? PhysicalSizeOf(0) : batch.front().px;
                    workerFaces_ = ftw::createDefaultFontFaces(workerFacePx_, workerLib_);
                    workerFaceSizes_.assign(workerFaces_.size(), workerFacePx_);
                }
                catch (const std::exception&) {
                    workerFaces_.clear();
                    workerLib_.reset();
                }
            }

            std::vector<GlyphDone> produced;
            produced.reserve(batch.size());

            for (const GlyphTask& t : batch) {
                if (stopping_.load(std::memory_order_relaxed)) return;

                GlyphDone d;
                d.task = t;
                if (t.epoch != epoch_.load(std::memory_order_relaxed) || workerFaces_.empty()) {
                    d.found = false;
                    produced.push_back(std::move(d));
                    continue;
                }
                if (EnsureFacesAtSize(workerFaces_, workerFaceSizes_, t.px, workerFacePx_)) {
                }
                d.found = RasterizeFrom(workerFaces_, t.cp, d.bmp);
                produced.push_back(std::move(d));
            }

            if (!produced.empty()) {
                std::function<void()> cb;
                {
                    std::lock_guard<std::mutex> lk(mtx_);
                    for (auto& d : produced) done_.push_back(std::move(d));
                    cb = wake_;
                }
                if (cb) cb();
            }
        }
    }

    bool FontRenderer::PipelineIdle() const {
        return queued_.load(std::memory_order_relaxed) == 0;
    }

    bool FontRenderer::HasGlyphWork() const {
        if (queued_.load(std::memory_order_relaxed) > 0) return true;
        std::lock_guard<std::mutex> lk(mtx_);
        return !tasks_.empty() || !done_.empty();
    }

    int FontRenderer::PendingGlyphCount() const {
        return queued_.load(std::memory_order_relaxed);
    }

    bool FontRenderer::Enqueue(char32_t cp, unsigned int pxPhys) {
        const uint64_t ep = epoch_.load(std::memory_order_relaxed);
        {
            std::lock_guard<std::mutex> lk(mtx_);
            if (tasks_.size() + done_.size() >= kMaxQueuedTasks) return false;
            tasks_.push_back(GlyphTask{ pxPhys, cp, ep });
            queued_.fetch_add(1, std::memory_order_relaxed);
        }
        cv_.notify_one();
        return true;
    }

    bool FontRenderer::RasterizeSync(char32_t cp, unsigned int pxPhys, GlyphTex& out) {
        if (!sink_) return false;
        EnsureFaceSize(pxPhys);

        ftw::GlyphBitmap bmp;
        if (!RasterizeFrom(faces_, cp, bmp)) return false;

        out = GlyphTex{};
        out.w = bmp.width;  out.h = bmp.height;
        out.bearingX = bmp.bearingX;  out.bearingY = bmp.bearingY;
        out.advanceX = bmp.advanceX;
        out.isColor = bmp.isColor;

        if (bmp.width > 0 && bmp.height > 0 && !bmp.buffer.empty())
            out.tex = CreateGlyphTexture(sink_, bmp, bmp.pitch);

        if (!out.tex) return false;
        out.state = GState::Ready;
        ++contentEpoch_;
        return true;
    }

    const FontRenderer::GlyphTex* FontRenderer::Resolve(char32_t cp, unsigned int pxPhys) {
        if (!sink_) return nullptr;

        SizeCache& bucket = caches_[pxPhys];

        GlyphTex* e = nullptr;
        auto it = bucket.glyphs.find(cp);
        if (it != bucket.glyphs.end()) {
            e = &it->second;
            if (e->state != GState::Pending) return e;
        }
        else {
            e = &bucket.glyphs.emplace(cp, GlyphTex{}).first->second;
            e->state = GState::Pending;
        }

        if (syncUsed_ < syncBudget_) {
            ++syncUsed_;
            if (RasterizeSync(cp, pxPhys, *e)) {
                e->state = GState::Ready;
            }
            else {
                bool has = false;
                for (auto& f : faces_) {
                    if (f->hasGlyph(cp)) { has = true; break; }
                }
                *e = GlyphTex{};
                e->state = has ? GState::Pending : GState::Missing;
            }
            return e;
        }

        if (Enqueue(cp, pxPhys)) e->state = GState::Queued;
        return e;
    }

    void FontRenderer::CommitResult(GlyphDone&& r) {
        if (r.task.epoch != epoch_.load(std::memory_order_relaxed)) return;

        SizeCache& bucket = caches_[r.task.px];
        auto it = bucket.glyphs.find(r.task.cp);
        GlyphTex* e = (it != bucket.glyphs.end()) ? &it->second : nullptr;
        if (!e) {
            e = &bucket.glyphs.emplace(r.task.cp, GlyphTex{}).first->second;
        }

        if (!r.found || r.bmp.width <= 0 || r.bmp.height <= 0 || r.bmp.buffer.empty()) {
            *e = GlyphTex{};
            e->state = GState::Missing;
            return;
        }

        GlyphTex g;
        g.w = r.bmp.width;  g.h = r.bmp.height;
        g.bearingX = r.bmp.bearingX;  g.bearingY = r.bmp.bearingY;
        g.advanceX = r.bmp.advanceX;
        g.isColor = r.bmp.isColor;
        g.tex = CreateGlyphTexture(sink_, r.bmp, r.bmp.pitch);

        if (!g.tex) {
            *e = GlyphTex{};
            e->state = GState::Pending;
            return;
        }

        g.state = GState::Ready;
        *e = g;

        ++contentEpoch_;
    }

    int FontRenderer::PumpReadyGlyphs(int budget) {
        syncUsed_ = 0;
        return PumpInternal(budget);
    }

    int FontRenderer::PumpInternal(int budget) {
        if (budget <= 0) budget = 1;

        std::deque<GlyphDone> batch;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            while (!done_.empty() && static_cast<int>(batch.size()) < budget) {
                batch.push_back(std::move(done_.front()));
                done_.pop_front();
            }
        }

        int uploaded = 0;
        for (GlyphDone& d : batch) {
            int cur = queued_.load(std::memory_order_relaxed);
            while (cur > 0 &&
                !queued_.compare_exchange_weak(cur, cur - 1, std::memory_order_relaxed)) {
            }
            CommitResult(std::move(d));
            ++uploaded;
        }

        if (PipelineIdle()) syncUsed_ = 0;

        return uploaded;
    }

    std::vector<GlyphQuad> FontRenderer::LayoutText(std::string_view utf8,
        const FontDesc& font,
        float baseX, float baseY) {
        std::vector<GlyphQuad> out;
        if (faces_.empty()) {
            if (!utf8.empty()) ++missCount_;
            return out;
        }
        if (utf8.empty()) return out;

        bool hasDone = false;
        {
            std::lock_guard<std::mutex> lk(mtx_);
            hasDone = !done_.empty();
        }
        if (hasDone) PumpInternal(kInlinePumpBudget);

        const unsigned int px = font.pixelSize;

        const float s = (scale_ > 0.0f) ? scale_ : 1.0f;

        const float ox = std::round(baseX * s) / s;
        const float oy = std::round(baseY * s) / s;

        const unsigned int pxPhys = PhysicalSizeOf(px);

        ftw::CodePointCursor cursor(utf8);

        float pen = 0.0f;
        char32_t cp = 0;
        while (cursor.next(cp)) {
            if (IsLayoutControl(cp)) continue;

            const GlyphTex* g = Resolve(cp, pxPhys);
            if (g && g->drawable()) {
                GlyphQuad q;
                q.x = ox + pen + (float)g->bearingX / s;
                q.y = oy - (float)g->bearingY / s;
                q.w = (float)g->w / s;
                q.h = (float)g->h / s;
                q.u0 = 0; q.v0 = 0; q.u1 = 1; q.v1 = 1;
                q.texture = g->tex;
                q.isColor = g->isColor;
                out.push_back(q);
            }
            else {
                ++missCount_;
            }

            pen += static_cast<float>(AdvanceOf(cp, pxPhys)) / s;
        }
        return out;
    }

    int FontRenderer::AdvanceOf(char32_t cp, unsigned int pxPhys) {
        SizeCache& bucket = caches_[pxPhys];
        auto it = bucket.advances.find(cp);
        if (it != bucket.advances.end()) return it->second;

        EnsureFaceSize(pxPhys);
        int adv = 0;
        for (auto& f : faces_) {
            if (!f->hasGlyph(cp)) continue;
            adv = f->advanceOf(cp);
            break;
        }
        bucket.advances.emplace(cp, adv);
        return adv;
    }

    TextMetrics FontRenderer::MeasureText(std::string_view utf8, const FontDesc& font) {
        TextMetrics m;
        if (faces_.empty() || utf8.empty()) return m;

        const unsigned int px = font.pixelSize;

        const float s = (scale_ > 0.0f) ? scale_ : 1.0f;
        const unsigned int pxPhys = PhysicalSizeOf(px);

        float penX = 0.0f;
        ftw::CodePointCursor cursor(utf8);
        char32_t cp = 0;
        while (cursor.next(cp)) {
            if (IsLayoutControl(cp)) continue;
            penX += static_cast<float>(AdvanceOf(cp, pxPhys)) / s;
        }
        m.width = penX;

        EnsureFaceSize(pxPhys);
        const int asc = faces_[0]->ascender();
        const int desc = faces_[0]->descender();
        m.ascent = static_cast<float>(asc) / s;
        m.descent = static_cast<float>(desc > 0 ? desc : -desc) / s;
        m.height = m.ascent + m.descent;
        return m;
    }

}
