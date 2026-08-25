#include <l2/transport/file_source.h>
#include <l2/common/types.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <cstring>
#include <stdexcept>

FileSource::FileSource(const char* path) {
    fd_ = open(path, O_RDONLY);

    if (fd_ < 0) throw std::runtime_error("open failed");

    struct stat st{};
    ::fstat(fd_, &st);

    size_ = size_t(st.st_size);
    void* p = ::mmap(nullptr, size_, PROT_READ, MAP_PRIVATE, fd_, 0);

    if (p == MAP_FAILED) throw std::runtime_error("mmap failed");
    ::madvise(p,size_, MADV_SEQUENTIAL);

    base_ = static_cast<const std::byte*>(p);
}

FileSource::~FileSource() {
    if (base_) ::munmap(const_cast<std::byte*>(base_), size_);
    if (fd_ >= 0) ::close(fd_);
}

bool FileSource::next(Frame& out) {
    constexpr size_t kHdr = 4 + 8 + 1;

    if (off_ + kHdr > size_) return false;

    uint32_t len; std::memcpy(&len, base_ + off_, 4);
    uint64_t ts; std::memcpy(&ts, base_ + off_ + 4, 8);
    uint8_t kind; std::memcpy(&kind, base_ + off_ + 12, 1);

    if (off_ + kHdr + len > size_) return false;

    out = Frame{
        {base_ + off_ + kHdr, len},
        ts,
        StreamKind{kind},
    };

    off_ += kHdr + len;
    return true;
}
