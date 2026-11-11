#include "pch.h"
#include "ZeImageStore.h"
#include "include/ZeEmbeddedRes.h"

#include <SOIL2/SOIL2.h>

#include <iostream>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Kit {

    ImageStore::~ImageStore() { Shutdown(); }

    ImageHandle ImageStore::Load(std::string_view path) {
        if (path.empty()) return 0;

        std::string key(path);

        auto it = byPath_.find(key);
        if (it != byPath_.end()) return it->second;

        int w = 0, h = 0, channels = 0;
        unsigned char* pixels = SOIL_load_image(key.c_str(), &w, &h, &channels, SOIL_LOAD_RGBA);
        if (!pixels || w <= 0 || h <= 0) {
            const char* why = SOIL_last_result();
            std::cerr << "[ImageStore] failed to load: " << key
                << " (" << (why ? why : "unknown") << ")\n";
            if (pixels) SOIL_free_image_data(pixels);

            byPath_[key] = 0;
            return 0;
        }

        return Commit(key, w, h, pixels);
    }

    ImageHandle ImageStore::Commit(const std::string& key, int w, int h,
        unsigned char* pixels) {
        uint32_t tex = 0;
        if (sink_) {
            tex = sink_->CreateRGBA(w, h, pixels, w * 4, false);
        }
        if (!tex) {
            std::cerr << "[ImageStore] no texture sink; cannot upload: " << key << "\n";
            SOIL_free_image_data(pixels);
            byPath_[key] = 0;
            return 0;
        }

        SOIL_free_image_data(pixels);

        const ImageHandle handle = next_++;
        entries_.push_back(Entry{ BackgroundImage{ tex, w, h }, key });
        byPath_[key] = handle;
        return handle;
    }

    ImageHandle ImageStore::LoadFromMemory(const std::string& key,
        const unsigned char* data, std::size_t size) {
        if (key.empty() || !data || size == 0) return 0;

        auto it = byPath_.find(key);
        if (it != byPath_.end()) return it->second;

        int w = 0, h = 0, channels = 0;
        unsigned char* pixels = SOIL_load_image_from_memory(
            data, static_cast<int>(size), &w, &h, &channels, SOIL_LOAD_RGBA);
        if (!pixels || w <= 0 || h <= 0) {
            const char* why = SOIL_last_result();
            std::cerr << "[ImageStore] failed to decode memory image: " << key
                << " (" << (why ? why : "unknown") << ")\n";
            if (pixels) SOIL_free_image_data(pixels);

            byPath_[key] = 0;
            return 0;
        }

        return Commit(key, w, h, pixels);
    }

    ImageHandle ImageStore::LoadFromResource(int resId) {
        if (resId <= 0) return 0;

        const std::string key = ":res:" + std::to_string(resId);
        auto it = byPath_.find(key);
        if (it != byPath_.end()) return it->second;

        const unsigned char* data = nullptr;
        std::size_t size = 0;

#ifdef _WIN32
        const HRSRC hrsrc = FindResourceW(GetModuleHandleW(nullptr),
            MAKEINTRESOURCEW(resId), RT_RCDATA);
        if (hrsrc) {
            const HGLOBAL hglob = LoadResource(nullptr, hrsrc);
            if (hglob) {
                data = static_cast<const unsigned char*>(LockResource(hglob));
                size = static_cast<std::size_t>(SizeofResource(nullptr, hrsrc));
            }
        }
#endif
        if (!data || size == 0) {
            const EmbeddedResource emb = FindEmbeddedResource(resId);
            data = static_cast<const unsigned char*>(emb.data);
            size = emb.size;
        }

        if (!data || size == 0) {
            std::cerr << "[ImageStore] embedded resource not found: " << resId
                      << "（非 Windows 平台需要先 Kit::RegisterEmbeddedResource 登记）\n";
            byPath_[key] = 0;
            return 0;
        }

        return LoadFromMemory(key, data, size);
    }

    const BackgroundImage* ImageStore::Get(ImageHandle h) const {
        if (h == 0 || h > entries_.size()) return nullptr;
        const BackgroundImage& img = entries_[h - 1].img;
        if (!img.handle) return nullptr;
        return &img;
    }

    bool ImageStore::SizeOf(std::string_view path, int& w, int& h) {
        const ImageHandle handle = Load(path);
        if (!handle) return false;

        const BackgroundImage* img = Get(handle);
        if (!img) return false;

        w = img->width;
        h = img->height;
        return true;
    }

    void ImageStore::Shutdown() {
        for (Entry& e : entries_) {
            if (e.img.handle) {
                if (sink_) sink_->Destroy(e.img.handle);
                e.img.handle = 0;
            }
        }
        entries_.clear();
        byPath_.clear();
        next_ = 1;
    }

}
