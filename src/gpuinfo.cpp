#include "gpuinfo.h"

#include <QStringList>

#ifdef _WIN32
#include <d3d11.h>
#include <dxgi.h>
#include <windows.h>
#endif

namespace
{
QString formatBytes(quint64 bytes)
{
    return QStringLiteral("%1 MB").arg(bytes / (1024ULL * 1024ULL));
}
} // namespace

static QList<GpuAdapterInfo> probeGpuAdapters()
{
    QList<GpuAdapterInfo> result;

#ifdef _WIN32
    IDXGIFactory1 *factory = nullptr;
    const HRESULT createResult =
        CreateDXGIFactory1(IID_IDXGIFactory1,
                           reinterpret_cast<void **>(&factory));
    if (FAILED(createResult) || factory == nullptr) {
        return result;
    }

    for (UINT index = 0;; ++index) {
        IDXGIAdapter1 *adapter = nullptr;
        if (factory->EnumAdapters1(index, &adapter) == DXGI_ERROR_NOT_FOUND) {
            break;
        }
        if (adapter == nullptr) {
            break;
        }

        DXGI_ADAPTER_DESC1 desc{};
        if (SUCCEEDED(adapter->GetDesc1(&desc))) {
            GpuAdapterInfo info;
            info.index = static_cast<int>(index);
            info.description = QString::fromWCharArray(desc.Description);
            info.vendorId = desc.VendorId;
            info.deviceId = desc.DeviceId;
            info.luid = (static_cast<quint64>(desc.AdapterLuid.HighPart) << 32) |
                        static_cast<quint64>(desc.AdapterLuid.LowPart);
            info.dedicatedVideoMemory = desc.DedicatedVideoMemory;
            info.sharedSystemMemory = desc.SharedSystemMemory;
            info.software = (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE) != 0;

#ifdef _WIN32
            if (!info.software) {
                const D3D_FEATURE_LEVEL levels[] = {
                    D3D_FEATURE_LEVEL_11_1,
                    D3D_FEATURE_LEVEL_11_0,
                    D3D_FEATURE_LEVEL_10_1,
                    D3D_FEATURE_LEVEL_10_0,
                };
                ID3D11Device *device = nullptr;
                D3D_FEATURE_LEVEL selectedLevel = D3D_FEATURE_LEVEL_10_0;
                const HRESULT deviceResult = D3D11CreateDevice(
                    adapter, D3D_DRIVER_TYPE_UNKNOWN, nullptr, 0, levels,
                    static_cast<UINT>(sizeof(levels) / sizeof(levels[0])),
                    D3D11_SDK_VERSION, &device, &selectedLevel, nullptr);
                if (SUCCEEDED(deviceResult) && device != nullptr) {
                    info.d3d11Device = true;
                    ID3D11VideoDevice *videoDevice = nullptr;
                    if (SUCCEEDED(device->QueryInterface(
                            __uuidof(ID3D11VideoDevice),
                            reinterpret_cast<void **>(&videoDevice))) &&
                        videoDevice != nullptr) {
                        const UINT profileCount =
                            videoDevice->GetVideoDecoderProfileCount();
                        for (UINT profile = 0; profile < profileCount; ++profile) {
                            GUID guid{};
                            if (SUCCEEDED(videoDevice->GetVideoDecoderProfile(
                                    profile, &guid))) {
                                if (IsEqualGUID(
                                        guid,
                                        D3D11_DECODER_PROFILE_H264_VLD_NOFGT)) {
                                    info.h264Decode = true;
                                }
                                if (IsEqualGUID(
                                        guid,
                                        D3D11_DECODER_PROFILE_HEVC_VLD_MAIN)) {
                                    info.h265Decode = true;
                                }
                            }
                        }
                        videoDevice->Release();
                    }
                    device->Release();
                }
            }
#endif
            result.append(info);
        }
        adapter->Release();
    }

    factory->Release();
#endif

    return result;
}

QList<GpuAdapterInfo> enumerateGpuAdapters()
{
    // D3D11 能力探測會建立驅動裝置與工作執行緒。診斷每秒更新時只讀取
    // 同一份快照，避免反覆載入驅動並干擾正在進行的音訊與影片串流。
    static const QList<GpuAdapterInfo> adapters = probeGpuAdapters();
    return adapters;
}

QStringList gpuAdapterLogLines()
{
    QStringList lines;
    const QList<GpuAdapterInfo> adapters = enumerateGpuAdapters();
    if (adapters.isEmpty()) {
        lines.append(QStringLiteral("GPU adapters: none reported by DXGI"));
        return lines;
    }

    lines.append(QStringLiteral("GPU adapters: %1").arg(adapters.size()));
    for (const GpuAdapterInfo &adapter : adapters) {
        lines.append(
            QStringLiteral(
                "  [%1] %2 vendor=0x%3 device=0x%4 luid=0x%5 "
                "dedicated=%6 shared=%7%8")
                .arg(adapter.index)
                .arg(adapter.description)
                .arg(adapter.vendorId, 4, 16, QLatin1Char('0'))
                .arg(adapter.deviceId, 4, 16, QLatin1Char('0'))
                .arg(adapter.luid, 16, 16, QLatin1Char('0'))
                .arg(formatBytes(adapter.dedicatedVideoMemory))
                .arg(formatBytes(adapter.sharedSystemMemory))
                .arg(adapter.software ? QStringLiteral(" software")
                                      : adapter.d3d11Device
                                            ? (adapter.h264Decode
                                                   ? (adapter.h265Decode
                                                          ? QStringLiteral(" h264+h265")
                                                          : QStringLiteral(" h264"))
                                                   : QStringLiteral(" no-hw-decode"))
                                            : QStringLiteral(" no-d3d11")));
    }
    return lines;
}

int selectGpuAdapter(bool needH265)
{
    int bestIndex = -1;
    quint64 bestMemory = 0;

    const QList<GpuAdapterInfo> adapters = enumerateGpuAdapters();
    for (const GpuAdapterInfo &adapter : adapters) {
        if (adapter.software || !adapter.d3d11Device) {
            continue;
        }
        if (!adapter.h264Decode) {
            continue;
        }
        if (needH265 && !adapter.h265Decode) {
            continue;
        }
        if (bestIndex < 0 || adapter.dedicatedVideoMemory > bestMemory) {
            bestIndex = adapter.index;
            bestMemory = adapter.dedicatedVideoMemory;
        }
    }
    return bestIndex;
}
