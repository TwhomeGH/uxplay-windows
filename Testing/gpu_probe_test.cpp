// Count actual D3D11 probes while repeatedly reading diagnostic snapshots.
#include <d3d11.h>
#include <cassert>
#include <cstdio>

static unsigned probeCount = 0;
static HRESULT WINAPI countedCreateDevice(
    IDXGIAdapter *adapter, D3D_DRIVER_TYPE type, HMODULE software, UINT flags,
    const D3D_FEATURE_LEVEL *levels, UINT count, UINT sdk, ID3D11Device **device,
    D3D_FEATURE_LEVEL *selected, ID3D11DeviceContext **context)
{
    ++probeCount;
    return D3D11CreateDevice(adapter, type, software, flags, levels, count, sdk,
                             device, selected, context);
}

#define D3D11CreateDevice countedCreateDevice
#include "../src/gpuinfo.cpp"
#undef D3D11CreateDevice

int main()
{
    const auto initial = enumerateGpuAdapters();
    const unsigned firstProbes = probeCount;
    assert(!initial.isEmpty() && firstProbes > 0);
    for (int i = 0; i < 1000; ++i) {
        assert(enumerateGpuAdapters().size() == initial.size());
        assert(!gpuAdapterLogLines().isEmpty());
        selectGpuAdapter(true);
    }
    assert(probeCount == firstProbes);
    std::printf("GPU snapshot test passed: initial probes=%u, refresh probes=0\n", firstProbes);
}
