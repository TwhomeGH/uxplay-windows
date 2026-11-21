#ifndef UXPLAY_GPUINFO_H
#define UXPLAY_GPUINFO_H

#include <QList>
#include <QString>
#include <QStringList>
#include <QtGlobal>

/// Describes one DXGI adapter visible to this process.
///
/// The app runs on hybrid-GPU machines (an integrated AMD/Intel GPU plus a
/// discrete NVIDIA/AMD GPU), and Windows may assign the decoder, the D3D11
/// video sink, OBS and the Frame Server to different adapters. Logging the
/// DXGI list and LUIDs is the first step in diagnosing a video pipeline that
/// only stalls on one adapter.
struct GpuAdapterInfo
{
    int index = -1;
    QString description;
    unsigned int vendorId = 0;
    unsigned int deviceId = 0;
    quint64 luid = 0;
    quint64 dedicatedVideoMemory = 0;
    quint64 sharedSystemMemory = 0;
    bool software = false;
    bool d3d11Device = false;
    bool h264Decode = false;
    bool h265Decode = false;
};

/// Returns a process-lifetime snapshot of DXGI adapters, in DXGI order.
/// The first call probes D3D11 capabilities; subsequent calls reuse the result
/// without creating devices. Restart the app to refresh after a GPU change.
QList<GpuAdapterInfo> enumerateGpuAdapters();

/// Chooses a hardware adapter that can actually decode the requested codec.
/// Software adapters and adapters without a D3D11 video decoder are skipped.
/// Returns -1 when no suitable adapter is available.
int selectGpuAdapter(bool needH265);

/// Returns one log-friendly line per adapter (index, name, LUID, memory).
QStringList gpuAdapterLogLines();

#endif
