#pragma once
#include <l2/common/types.h>

class IFrameSource {
public:
    IFrameSource() = default;
    virtual ~IFrameSource() = default;

    IFrameSource(const IFrameSource&) = delete;
    IFrameSource& operator=(const IFrameSource&) = delete;

    IFrameSource(IFrameSource&&) = default;
    IFrameSource& operator=(IFrameSource&&) = default;

    virtual bool next (Frame& out) = 0;
};
