//
// Copyright (C) Microsoft Corporation. All rights reserved.
//
#include "pch.h"
#include "SharedFrame.h"
#include <algorithm>
#include <cstring>
#include <sddl.h>

#pragma comment(lib, "advapi32.lib")

namespace
{
    constexpr DWORD kMaxPipeFrameSize =
        2360u * 1640u * 4u + sizeof(uxplay_vcam::FrameHeader);
}

SimpleFrameGenerator::~SimpleFrameGenerator()
{
    StopFramePipe();
}

HRESULT SimpleFrameGenerator::Initialize(_In_ IMFMediaType* pMediaType)
{
    RETURN_HR_IF_NULL(E_INVALIDARG, pMediaType);

    RETURN_IF_FAILED(pMediaType->GetGUID(MF_MT_SUBTYPE, &m_subType));
    if (m_subType != MFVideoFormat_RGB32 && m_subType != MFVideoFormat_NV12)
    {
        RETURN_HR_MSG(MF_E_UNSUPPORTED_FORMAT, "Unsupported format: %s", winrt::to_hstring(m_subType).data());
    }
    MFGetAttributeSize(pMediaType, MF_MT_FRAME_SIZE, &m_width, &m_height);

    StartFramePipe();
    return S_OK;
}

void SimpleFrameGenerator::StartFramePipe()
{
    if (m_pipe != nullptr)
    {
        return;
    }

    PSECURITY_DESCRIPTOR sd = nullptr;
    SECURITY_ATTRIBUTES sa{};
    sa.nLength = sizeof(sa);
    if (ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;;GA;;;WD)", SDDL_REVISION_1, &sd, nullptr))
    {
        sa.lpSecurityDescriptor = sd;
    }

    m_pipe = CreateNamedPipeW(
        uxplay_vcam::kPipeName,
        PIPE_ACCESS_INBOUND,
        PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
        1,
        0,
        kMaxPipeFrameSize,
        0,
        sa.lpSecurityDescriptor ? &sa : nullptr);

    if (sd != nullptr)
    {
        LocalFree(sd);
    }

    if (m_pipe == INVALID_HANDLE_VALUE)
    {
        m_pipe = nullptr;
        DEBUG_MSG(L"Virtual camera frame pipe unavailable (error %lu)\n", GetLastError());
        return;
    }

    DEBUG_MSG(L"Virtual camera frame pipe ready: %s\n", uxplay_vcam::kPipeName);

    m_pipeStop = false;
    try
    {
        m_pipeThread = std::thread(&SimpleFrameGenerator::FramePipeThread, this);
    }
    catch (...)
    {
        CloseHandle(m_pipe);
        m_pipe = nullptr;
        DEBUG_MSG(L"Virtual camera frame pipe thread failed\n");
        return;
    }

    DEBUG_MSG(L"Virtual camera frame pipe ready: %s\n", uxplay_vcam::kPipeName);
}

void SimpleFrameGenerator::StopFramePipe()
{
    m_pipeStop = true;

    if (m_pipe != nullptr)
    {
        CancelIoEx(m_pipe, nullptr);
        DisconnectNamedPipe(m_pipe);
    }

    if (m_pipeThread.joinable())
    {
        m_pipeThread.join();
    }

    if (m_pipe != nullptr)
    {
        CloseHandle(m_pipe);
        m_pipe = nullptr;
    }
}

void SimpleFrameGenerator::FramePipeThread()
{
    std::vector<BYTE> buffer(kMaxPipeFrameSize);

    while (!m_pipeStop)
    {
        BOOL connected = ConnectNamedPipe(m_pipe, nullptr);
        if (!connected)
        {
            DWORD error = GetLastError();
            if (error != ERROR_PIPE_CONNECTED)
            {
                if (m_pipeStop)
                {
                    break;
                }
                Sleep(100);
                continue;
            }
        }

        while (!m_pipeStop)
        {
            DWORD bytesRead = 0;
            if (!ReadFile(m_pipe, buffer.data(), static_cast<DWORD>(buffer.size()), &bytesRead, nullptr))
            {
                break;
            }

            if (bytesRead < sizeof(uxplay_vcam::FrameHeader))
            {
                continue;
            }

            auto* header = reinterpret_cast<const uxplay_vcam::FrameHeader*>(buffer.data());
            if (header->magic != uxplay_vcam::kMagic ||
                header->version != uxplay_vcam::kVersion ||
                header->header_size != sizeof(uxplay_vcam::FrameHeader) ||
                header->data_size == 0 ||
                header->data_size > bytesRead - sizeof(uxplay_vcam::FrameHeader))
            {
                continue;
            }

            {
                std::lock_guard<std::mutex> lock(m_pipeMutex);
                m_pipeHeader = *header;
                m_pipeFrame.assign(
                    buffer.begin() + sizeof(uxplay_vcam::FrameHeader),
                    buffer.begin() + sizeof(uxplay_vcam::FrameHeader) + header->data_size);
                m_hasPipeFrame = true;
            }
        }

        DisconnectNamedPipe(m_pipe);
    }
}

bool SimpleFrameGenerator::ReadFrameFromPipe(
    _Inout_updates_bytes_(len) BYTE* pBuf,
    _In_ DWORD len,
    _In_ LONG pitch)
{
    if (pBuf == nullptr)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(m_pipeMutex);
    if (!m_hasPipeFrame || m_pipeFrame.empty())
    {
        return false;
    }

    const auto& header = m_pipeHeader;
    const uint32_t expectedFormat = static_cast<uint32_t>(
        m_subType == MFVideoFormat_RGB32 ? uxplay_vcam::PixelFormat::Rgb32 : uxplay_vcam::PixelFormat::Nv12);

    if (header.magic != uxplay_vcam::kMagic ||
        header.version != uxplay_vcam::kVersion ||
        header.header_size != sizeof(uxplay_vcam::FrameHeader) ||
        header.width != m_width ||
        header.height != m_height ||
        header.format != expectedFormat ||
        header.data_size > m_pipeFrame.size() ||
        header.data_size > len)
    {
        return false;
    }

    const BYTE* source = m_pipeFrame.data();
    const uint32_t sourceStride = header.stride;
    const uint32_t copyStride = (std::min)(sourceStride, static_cast<uint32_t>(pitch));
    uint32_t rows = m_height;
    if (m_subType == MFVideoFormat_NV12)
    {
        rows += m_height / 2;
    }

    for (uint32_t row = 0; row < rows; row++)
    {
        memcpy(pBuf + (size_t)row * pitch, source + (size_t)row * sourceStride, copyStride);
    }

    return true;
}

/*:
   Writes to a buffer representing a 2D image.
   Writes a different constant to each line based on row number and current time.
   Assumes top down image, no negative stride and pBuf points to the begnning of the buffer of length len.
   Param:
   pBuf - pointer to beginning of buffer
   pitch - line length in bytes
   len - length of buffer in bytes
*/
HRESULT SimpleFrameGenerator::CreateFrame(
    _Inout_updates_bytes_(len) BYTE* pBuf,
    _In_ DWORD len,
    _In_ LONG pitch,
    _In_ ULONG rgbMask)
{
    if (m_subType == MFVideoFormat_RGB32)
    {
        if (ReadFrameFromPipe(pBuf, len, pitch))
        {
            return S_OK;
        }

        DEBUG_MSG(L"RGB32 frames %s\n", winrt::to_hstring(MFVideoFormat_RGB32).data());

        RETURN_IF_FAILED(_CreateRGB32Frame(pBuf, len, pitch, m_width, m_height, rgbMask));
    }
    else if(m_subType == MFVideoFormat_NV12)
    {
        if (ReadFrameFromPipe(pBuf, len, pitch))
        {
            return S_OK;
        }

        DEBUG_MSG(L"NV12 frames %s \n", winrt::to_hstring(MFVideoFormat_NV12).data());

        DWORD frameBuffLen = m_width * m_height * 4;
        wil::unique_cotaskmem_ptr<BYTE[]> spBuff = wil::make_unique_cotaskmem_nothrow<BYTE[]>(frameBuffLen);
        RETURN_IF_NULL_ALLOC(spBuff.get());

        RETURN_IF_FAILED(_CreateRGB32Frame(spBuff.get(), frameBuffLen, m_width * 4, m_width, m_height, rgbMask));
        RETURN_IF_FAILED(RGB32ToNV12Frame(spBuff.get(), frameBuffLen, m_width * 4, m_width, m_height, pBuf, len, pitch));
    }
    else
    {
        return MF_E_UNSUPPORTED_FORMAT;
    }

    return S_OK;
}

//////////////////////////////////////////////////
// private

HRESULT SimpleFrameGenerator::_CreateRGB32Frame(
    _Inout_updates_bytes_(len) BYTE* pBuf,
    _In_ DWORD len,
    _In_ LONG pitch,
    _In_ DWORD width,
    _In_ DWORD height,
    _In_ ULONG rgbMask )
{
    RETURN_HR_IF_NULL(E_INVALIDARG, pBuf);
    if (len < (abs(pitch) * height ))
    {
        return HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER);
    }

    LONGLONG curSysTimeInS = MFGetSystemTime() / (MFTIME)10000000;
    int offset = curSysTimeInS % height;

    for (unsigned int r = 0; r < height; r++)
    {
        uint32_t* p = (uint32_t*)(pBuf + (r * pitch));
        for (unsigned int c = 0; c < width; c++)
        {
            BYTE gray = (BYTE)(r + offset);
            *p = ((uint32_t)gray << 16 | (uint32_t)gray << 8 | (uint32_t)gray) & rgbMask;
            p++;
        }
    }

    return S_OK;
}

//////////////////////////////////////////////////
// pixelFormatConverter

void SimpleFrameGenerator::RGB24ToYUY2(int R, int G, int B, BYTE* pY, BYTE* pU, BYTE* pV)
{
    *pY = ((66 * R + 129 * G + 25 * B + 128) >> 8) + 16;
    *pU = ((-38 * R - 74 * G + 112 * B + 128) >> 8) + 128;
    *pV = ((112 * R - 94 * G - 18 * B + 128) >> 8) + 128;
}

void SimpleFrameGenerator::RGB24ToY(int R, int G, int B, BYTE* pY)
{
    *pY = ((66 * R + 129 * G + 25 * B + 128) >> 8) + 16;
}

void SimpleFrameGenerator::RGB32ToNV12(BYTE RGB1[8], BYTE RGB2[8], BYTE* pY1, BYTE* pY2, BYTE* pUV)
{
    RGB24ToYUY2(RGB1[2], RGB1[1], RGB1[0], pY1, pUV, pUV + 1);
    RGB24ToY(RGB1[6], RGB1[5], RGB1[4], pY1 + 1);
    RGB24ToYUY2(RGB2[2], RGB2[1], RGB2[0], pY2, pUV, pUV + 1);
    RGB24ToY(RGB2[6], RGB2[5], RGB2[4], pY2 + 1);
};

//////////////////////////////////////////////////
// FrameFormatConverter

HRESULT SimpleFrameGenerator::RGB32ToNV12Frame(_Inout_updates_bytes_(len) BYTE* pbBuff, ULONG cbBuff, long stride, UINT width, UINT height, BYTE* pbBuffOut, ULONG cbBuffOut, long strideOut)
{
    do
    {
        RETURN_HR_IF(E_UNEXPECTED, width * 4 * height > cbBuff);
        RETURN_HR_IF(E_UNEXPECTED, width * 1.5 * height > cbBuffOut);
        RETURN_HR_IF_NULL(E_INVALIDARG, pbBuff);

        RETURN_HR_IF_NULL(E_INVALIDARG, pbBuffOut);
        for (DWORD h = 0; h < height - 1; h += 2)
        {
            BYTE* pRGB1 = h * stride + pbBuff;
            BYTE* pRGB2 = (h + 1) * stride + pbBuff;
            BYTE* pY1 = h * strideOut + pbBuffOut;
            BYTE* pY2 = (h + 1) * strideOut + pbBuffOut;
            BYTE* pUV = (h / 2 + height) * strideOut + pbBuffOut;

            for (DWORD w = 0; w < width; w += 2)
            {
                RGB32ToNV12(pRGB1, pRGB2, pY1, pY2, pUV);
                pRGB1 += 8;
                pRGB2 += 8;
                pY1 += 2;
                pY2 += 2;
                pUV += 2;
            }
        }
    } while (FALSE);

    return S_OK;
}
