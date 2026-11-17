//
// Copyright (C) Microsoft Corporation. All rights reserved.
//

#pragma once
#ifndef SIMPLE_FRAME_GENERATOR_H
#define SIMPLE_FRAME_GENERATOR_H

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include "SharedFrame.h"

class SimpleFrameGenerator
{
public:
    SimpleFrameGenerator() = default;
    ~SimpleFrameGenerator();

    HRESULT Initialize(_In_ IMFMediaType* pMediaType);

    HRESULT CreateFrame(
        _Inout_updates_bytes_(len) BYTE* pBuf,
        _In_ DWORD len,
        _In_ LONG pitch,
        _In_ ULONG rgbMask);

    // pixel format converter
    static void RGB24ToYUY2(int R, int G, int B, BYTE* pY, BYTE* pU, BYTE* pV);
    static void RGB24ToY(int R, int G, int B, BYTE* pY);
    static void RGB32ToNV12(BYTE RGB1[8], BYTE RGB2[8], BYTE* pY1, BYTE* pY2, BYTE* pUV);

    static HRESULT RGB32ToNV12Frame(_Inout_updates_bytes_(len) BYTE* pbBuff, ULONG cbBuff, long stride, UINT width, UINT height, BYTE* pbBuffOut, ULONG cbBuffOut, long strideOut);

private:
    HRESULT _CreateRGB32Frame(
        _Inout_updates_bytes_(len) BYTE* pBuf,
        _In_ DWORD len,
        _In_ LONG pitch,
        _In_ DWORD width,
        _In_ DWORD height,
        _In_ ULONG rgbMask);

    void StartFramePipe();
    void StopFramePipe();
    void FramePipeThread();
    bool ReadFrameFromPipe(
        _Inout_updates_bytes_(len) BYTE* pBuf,
        _In_ DWORD len,
        _In_ LONG pitch);

    UINT32 m_width = 0;
    UINT32 m_height = 0;
    GUID m_subType = GUID_NULL;

    // The media source creates the pipe; the interactive UxPlay process connects
    // as a writer.  The reader thread owns the pipe and publishes the latest
    // complete frame through m_pipeFrame.
    HANDLE m_pipe = nullptr;
    HANDLE m_pipeThreadHandle = nullptr;
    std::thread m_pipeThread;
    std::atomic<bool> m_pipeStop{false};
    std::mutex m_pipeMutex;
    std::vector<BYTE> m_pipeFrame;
    uxplay_vcam::FrameHeader m_pipeHeader{};
    bool m_hasPipeFrame = false;
};

#endif
