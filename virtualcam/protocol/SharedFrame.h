//
// Frame protocol between the UxPlay Windows producer and the virtual camera
// media source.  The media source runs inside the Windows Frame Server, so a
// named pipe is used instead of a session-local shared mapping; the media
// source creates the pipe with an ACL that lets the interactive producer
// connect, and keeps only the latest frame.
//

#pragma once
#ifndef UXPLAY_SHARED_FRAME_H
#define UXPLAY_SHARED_FRAME_H

#include <cstdint>

namespace uxplay_vcam
{
    constexpr uint32_t kMagic = 0x43565855;  // "UXVC" little-endian.
    constexpr uint32_t kVersion = 1;

    constexpr wchar_t kPipeName[] = L"\\\\.\\pipe\\uxplay-vcam-frame-v1";

    enum class PixelFormat : uint32_t
    {
        Rgb32 = 1,
        Nv12 = 2,
    };

#pragma pack(push, 1)
    struct FrameHeader
    {
        uint32_t magic;
        uint32_t version;
        uint32_t header_size;
        uint32_t width;
        uint32_t height;
        uint32_t stride;
        uint32_t format;
        uint32_t data_size;
        uint64_t frame_id;
        uint64_t timestamp_ms;
    };
#pragma pack(pop)

    static_assert(sizeof(FrameHeader) == 48, "unexpected FrameHeader layout");
}

#endif
