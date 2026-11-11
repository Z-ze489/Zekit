#include "pch.h"
#include "include/ZeEmbeddedRes.h"

#include <mutex>
#include <unordered_map>

namespace Kit {

    namespace {

        using Table = std::unordered_map<int, EmbeddedResource>;

        Table& Registry() {
            static Table table;
            return table;
        }

        std::mutex& RegistryMutex() {
            static std::mutex m;
            return m;
        }

    }

    void RegisterEmbeddedResource(int id, const void* data, std::size_t size) {
        if (id <= 0 || !data || size == 0) return;

        std::lock_guard<std::mutex> lock(RegistryMutex());
        Registry()[id] = EmbeddedResource{ data, size };
    }

    EmbeddedResource FindEmbeddedResource(int id) {
        if (id <= 0) return {};

        std::lock_guard<std::mutex> lock(RegistryMutex());
        const Table& table = Registry();
        const auto it = table.find(id);
        return (it == table.end()) ? EmbeddedResource{} : it->second;
    }

}
