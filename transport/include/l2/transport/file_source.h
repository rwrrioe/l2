#pragma once
#include <l2/transport/frame_source.h>
#include <utility>
#include <cstddef>

class FileSource final : public IFrameSource {
    const std::byte* base_ = nullptr;
    size_t size_ = 0, off_ = 0;
    int fd_ = -1;
public:
    explicit FileSource(const char* path);
    ~FileSource();
    FileSource(const FileSource&) = delete;
    FileSource& operator=(const FileSource&) = delete;

    friend void swap(FileSource& a, FileSource&b) noexcept {
        using std::swap;
        swap(a.base_, b.base_);
        swap(a.size_, b.size_);
        swap(a.off_, b.off_);
        swap(a.fd_, b.fd_);
    }

    FileSource(FileSource&& other) noexcept {
        swap(*this, other);
    }

    FileSource& operator=(FileSource&& other) noexcept {
        FileSource tmp(std::move(other));
        swap(*this,tmp);
        return *this;
     }


    bool next(Frame& f) override;
};
