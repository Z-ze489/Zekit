#include "pch.h"

#include <utility>

#include "include/ZeFileDialog.h"
#include "include/Zekit.h"

namespace Kit {

    FileDialog& FileDialog::SetTitle(std::string title) {
        title_ = std::move(title);
        return *this;
    }

    FileDialog& FileDialog::SetInitialDirectory(std::string dir) {
        initialDir_ = std::move(dir);
        return *this;
    }

    FileDialog& FileDialog::SetDefaultName(std::string name) {
        defaultName_ = std::move(name);
        return *this;
    }

    FileDialog& FileDialog::SetDefaultExt(std::string ext) {
        defaultExt_ = std::move(ext);
        return *this;
    }

    FileDialog& FileDialog::AddFilter(std::string label, std::string patterns) {
        filterLabels_.push_back(std::move(label));
        filterPatterns_.push_back(std::move(patterns));
        return *this;
    }

    FileDialog& FileDialog::ClearFilters() {
        filterLabels_.clear();
        filterPatterns_.clear();
        filterIndex_ = 0;
        return *this;
    }

    FileDialog& FileDialog::SetFilterIndex(int index) {
        filterIndex_ = index < 0 ? 0 : index;
        return *this;
    }

    FileDialog& FileDialog::AllowMultiple(bool on) {
        allowMultiple_ = on;
        return *this;
    }

    FileDialog& FileDialog::SetOverwritePrompt(bool on) {
        overwritePrompt_ = on;
        return *this;
    }

    void FileDialog::Clear() {
        ok_ = false;
        files_.clear();
        first_.clear();
    }

    bool FileDialog::IsSupported(const Window& owner) {
        return owner.Supports(Capability::NativeFileDialog);
    }

    bool FileDialog::show(Window& owner, FileDialogDesc::Mode mode) {
        Clear();

        FileDialogDesc desc;
        desc.mode = mode;
        desc.title = title_;
        desc.initialDir = initialDir_;
        desc.defaultName = defaultName_;
        desc.defaultExt = defaultExt_;
        desc.filterLabels = filterLabels_;
        desc.filterPatterns = filterPatterns_;
        desc.filterIndex = filterIndex_;
        desc.allowMultiple = allowMultiple_ && (mode == FileDialogDesc::Mode::Open);
        desc.overwritePrompt = overwritePrompt_;

        std::vector<std::string> out;
        if (!owner.ShowFileDialog(desc, out)) return false;

        files_ = std::move(out);
        if (!files_.empty()) first_ = files_.front();
        ok_ = !files_.empty();
        return ok_;
    }

    bool FileDialog::ShowOpen(Window& owner) {
        return show(owner, FileDialogDesc::Mode::Open);
    }

    bool FileDialog::ShowSave(Window& owner) {
        return show(owner, FileDialogDesc::Mode::Save);
    }

    bool FileDialog::ShowPickFolder(Window& owner) {
        return show(owner, FileDialogDesc::Mode::PickFolder);
    }

}
