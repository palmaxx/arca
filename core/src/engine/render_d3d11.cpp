// arca_render_session — D3D11/DXGI HDR render data plane.
//
// Port of the engine fork's validated Phase-5a host (rapi_hdr_present.c)
// onto a dedicated render thread owned by the core:
//   - D3D11 hardware device + FLIP_DISCARD R10G10B10A2 swapchain
//   - BT.2020/PQ negotiation (SetColorSpace1) + HDR10 metadata, with SDR
//     fallback when the output can't present PQ
//   - display peak luminance from IDXGIOutput6::GetDesc1 -> TARGET_COLORSPACE
//   - per frame: GetBuffer(0) -> clear -> render(D3D11_TEX) -> Present(1, 0)
// The engine-side render context (pl-d3d11) is created and freed on the
// render thread itself.
//
// The backbuffer is wrapped directly: since engine fix P5b.1 (fork commit
// 008434c) the renderer releases its wrap of the host texture before
// mpv_render_context_render returns, so no reference survives the frame and
// ResizeBuffers is always legal. (ADR-004's intermediate-texture indirection
// was the workaround for the pre-fix engine; retired.) Border clearing
// stays core-side via a transient backbuffer RTV — the engine runs
// --border-background=none because wrapped R10G10B10A2 lacks blit_dst on
// some vendors (libplacebo cap model; unchanged by P5b.1).

#include "engine_internal.h"

#include "arca/arca_render.h"

#include <mpv/render.h>
#include <mpv/render_d3d11.h>

#include <d3d11.h>
#include <dxgi1_6.h>
#include <wrl/client.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdarg>
#include <cstdio>
#include <future>
#include <mutex>

using Microsoft::WRL::ComPtr;

// WinUI3 SwapChainPanel native interop (microsoft.ui.xaml.media.dxinterop.h,
// WindowsAppSDK 1.8). Declared locally so the core builds without the
// WinAppSDK headers; the IID is stable public API.
MIDL_INTERFACE("63aad0b8-7c24-40ff-85a8-640d944cc325")
IArcaSwapChainPanelNative : public IUnknown {
public:
    virtual HRESULT STDMETHODCALLTYPE SetSwapChain(IDXGISwapChain *swapChain) = 0;
};

struct arca_render_session {
    arca_engine *engine = nullptr;
    HWND hwnd = nullptr;            // hwnd session: target; panel: monitor ref
    ComPtr<IUnknown> panel_native;  // panel session only
    bool panel_mode = false;

    ComPtr<ID3D11Device> device;
    ComPtr<ID3D11DeviceContext> context;
    ComPtr<IDXGISwapChain3> swapchain;
    mpv_render_context *rctx = nullptr;

    std::thread thread;
    std::mutex mutex;
    std::condition_variable cv;
    bool wake = false;          // update callback / resize / quit pokes
    std::atomic<bool> quit{false};

    std::atomic<int> pending_w{0}, pending_h{0};
    std::atomic<bool> resize_pending{false};

    int width = 0, height = 0;
    std::atomic<bool> hdr_active{false};
    std::atomic<float> disp_max_nits{1000.0f};
    std::atomic<float> disp_min_nits{0.005f};

    // Panel composition scale; the inverse is applied as the swapchain
    // matrix transform so physical-pixel buffers map 1:1 onto the panel.
    std::atomic<float> scale_x{1.0f}, scale_y{1.0f};
    std::atomic<bool> scale_pending{false};
};

namespace {

void logf(arca_engine *e, const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    e->emit_log(buf);
}

void poke(arca_render_session *s) {
    {
        std::lock_guard<std::mutex> lock(s->mutex);
        s->wake = true;
    }
    s->cv.notify_one();
}

void on_render_update(void *ctx) {
    poke(static_cast<arca_render_session *>(ctx));
}

bool create_device(arca_render_session *s) {
    static const D3D_FEATURE_LEVEL levels[] = {
        D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
    };

    // Prefer the high-performance adapter: on hybrid laptops the default
    // (adapter 0) is the iGPU, which measurably cannot tone-map 4K60
    // (M2: ~46 VO drops/sec on Intel UHD vs the dGPU). Cross-adapter
    // present to an iGPU-owned panel is handled by the OS.
    ComPtr<IDXGIAdapter> adapter;
    ComPtr<IDXGIFactory6> factory6;
    if (SUCCEEDED(CreateDXGIFactory2(0, IID_PPV_ARGS(&factory6)))) {
        ComPtr<IDXGIAdapter1> a1;
        if (SUCCEEDED(factory6->EnumAdapterByGpuPreference(
                0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&a1)))) {
            adapter = a1;
            DXGI_ADAPTER_DESC1 desc{};
            if (SUCCEEDED(a1->GetDesc1(&desc))) {
                char name[128]{};
                WideCharToMultiByte(CP_UTF8, 0, desc.Description, -1, name,
                                    sizeof(name) - 1, nullptr, nullptr);
                logf(s->engine, "[arca-render] adapter: %s", name);
            }
        }
    }

    HRESULT hr = D3D11CreateDevice(
        adapter.Get(),
        adapter ? D3D_DRIVER_TYPE_UNKNOWN : D3D_DRIVER_TYPE_HARDWARE, nullptr,
        D3D11_CREATE_DEVICE_BGRA_SUPPORT, levels, ARRAYSIZE(levels),
        D3D11_SDK_VERSION, &s->device, nullptr, &s->context);
    if (FAILED(hr)) {
        logf(s->engine, "[arca-render] D3D11CreateDevice failed: 0x%08lx", hr);
        return false;
    }
    return true;
}

// Applies the inverse composition scale as the swapchain matrix transform
// (panel sessions; DXGI rejects it for hwnd swapchains).
void apply_scale(arca_render_session *s) {
    if (!s->panel_mode)
        return;
    ComPtr<IDXGISwapChain2> sc2;
    if (FAILED(s->swapchain.As(&sc2)))
        return;
    DXGI_MATRIX_3X2_F m{};
    m._11 = 1.0f / s->scale_x.load();
    m._22 = 1.0f / s->scale_y.load();
    sc2->SetMatrixTransform(&m);
}

bool create_swapchain(arca_render_session *s) {
    ComPtr<IDXGIFactory2> factory;
    HRESULT hr = CreateDXGIFactory2(0, IID_PPV_ARGS(&factory));
    if (FAILED(hr)) {
        logf(s->engine, "[arca-render] CreateDXGIFactory2 failed: 0x%08lx", hr);
        return false;
    }

    DXGI_SWAP_CHAIN_DESC1 scd{};
    scd.Width = static_cast<UINT>(s->width);
    scd.Height = static_cast<UINT>(s->height);
    scd.Format = DXGI_FORMAT_R10G10B10A2_UNORM;  // 10-bit; HDR10-capable
    scd.SampleDesc.Count = 1;
    // gpu-next renders with compute shaders -> UNORDERED_ACCESS required.
    scd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT | DXGI_USAGE_UNORDERED_ACCESS;
    scd.BufferCount = 2;
    scd.Scaling = DXGI_SCALING_STRETCH;
    scd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    scd.AlphaMode = DXGI_ALPHA_MODE_IGNORE;

    ComPtr<IDXGISwapChain1> sc1;
    if (s->panel_mode) {
        hr = factory->CreateSwapChainForComposition(s->device.Get(), &scd,
                                                    nullptr, &sc1);
        if (FAILED(hr)) {
            logf(s->engine,
                 "[arca-render] CreateSwapChainForComposition failed: 0x%08lx", hr);
            return false;
        }
    } else {
        hr = factory->CreateSwapChainForHwnd(s->device.Get(), s->hwnd, &scd,
                                             nullptr, nullptr, &sc1);
        if (FAILED(hr)) {
            logf(s->engine,
                 "[arca-render] CreateSwapChainForHwnd failed: 0x%08lx", hr);
            return false;
        }
        factory->MakeWindowAssociation(s->hwnd, DXGI_MWA_NO_ALT_ENTER);
    }
    if (FAILED(sc1.As(&s->swapchain))) {
        logf(s->engine, "[arca-render] IDXGISwapChain3 unavailable");
        return false;
    }
    apply_scale(s);
    return true;
}

// Negotiate the backbuffer colorspace: BT.2020/PQ when the output supports
// HDR10 presentation, BT.709/G22 otherwise. Returns whether HDR is active.
bool negotiate_colorspace(arca_render_session *s) {
    UINT support = 0;
    if (SUCCEEDED(s->swapchain->CheckColorSpaceSupport(
            DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020, &support)) &&
        (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)) {
        s->swapchain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G2084_NONE_P2020);

        // Same static metadata the windowed swapchain negotiates.
        ComPtr<IDXGISwapChain4> sc4;
        if (SUCCEEDED(s->swapchain.As(&sc4))) {
            DXGI_HDR_METADATA_HDR10 md{};
            md.RedPrimary[0] = 34000;  md.RedPrimary[1] = 16000;  // BT.2020
            md.GreenPrimary[0] = 13250; md.GreenPrimary[1] = 34500;
            md.BluePrimary[0] = 7500;  md.BluePrimary[1] = 3000;
            md.WhitePoint[0] = 15635;  md.WhitePoint[1] = 16450;
            md.MaxMasteringLuminance = 1000 * 10000;  // 0.0001 nit units
            md.MinMasteringLuminance = 50;            // 0.005 nit
            md.MaxContentLightLevel = 1000;
            md.MaxFrameAverageLightLevel = 400;
            sc4->SetHDRMetaData(DXGI_HDR_METADATA_TYPE_HDR10, sizeof(md), &md);
        }
        return true;
    }

    support = 0;
    if (SUCCEEDED(s->swapchain->CheckColorSpaceSupport(
            DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709, &support)) &&
        (support & DXGI_SWAP_CHAIN_COLOR_SPACE_SUPPORT_FLAG_PRESENT)) {
        s->swapchain->SetColorSpace1(DXGI_COLOR_SPACE_RGB_FULL_G22_NONE_P709);
    }
    logf(s->engine, "[arca-render] HDR10 not supported on this output "
                    "(display HDR off?) - presenting SDR");
    return false;
}

// The tone-map target must be the display's real peak, not the content's
// mastering peak (fork Phase-5 finding). Re-queried after resize so monitor
// moves are picked up. Located by the window's HMONITOR across *all*
// adapters: on hybrid GPUs the swapchain's GetContainingOutput fails when
// the render device isn't the adapter that owns the panel.
void query_display_hdr(arca_render_session *s) {
    HMONITOR monitor = MonitorFromWindow(s->hwnd, MONITOR_DEFAULTTONEAREST);
    if (!monitor)
        return;

    ComPtr<IDXGIFactory1> factory;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))))
        return;
    for (UINT ai = 0;; ai++) {
        ComPtr<IDXGIAdapter1> adapter;
        if (factory->EnumAdapters1(ai, &adapter) == DXGI_ERROR_NOT_FOUND)
            break;
        for (UINT oi = 0;; oi++) {
            ComPtr<IDXGIOutput> out;
            if (adapter->EnumOutputs(oi, &out) == DXGI_ERROR_NOT_FOUND)
                break;
            DXGI_OUTPUT_DESC od{};
            if (FAILED(out->GetDesc(&od)) || od.Monitor != monitor)
                continue;
            ComPtr<IDXGIOutput6> out6;
            DXGI_OUTPUT_DESC1 d{};
            if (SUCCEEDED(out.As(&out6)) && SUCCEEDED(out6->GetDesc1(&d)) &&
                d.MaxLuminance > 0) {
                s->disp_max_nits.store(d.MaxLuminance);
                s->disp_min_nits.store(d.MinLuminance);
                logf(s->engine,
                     "[arca-render] display: peak=%.0f nits, min=%.4f nits, "
                     "colorspace=%d", d.MaxLuminance, d.MinLuminance,
                     static_cast<int>(d.ColorSpace));
            }
            return;
        }
    }
}

bool create_render_context(arca_render_session *s) {
    mpv_d3d11_init_params init{};
    init.device = s->device.Get();
    mpv_render_param params[] = {
        {MPV_RENDER_PARAM_API_TYPE,
         const_cast<char *>(MPV_RENDER_API_TYPE_PL_D3D11)},
        {MPV_RENDER_PARAM_D3D11_INIT_PARAMS, &init},
        {MPV_RENDER_PARAM_INVALID, nullptr},
    };
    int err = mpv_render_context_create(&s->rctx, s->engine->mpv, params);
    if (err < 0) {
        logf(s->engine, "[arca-render] render_context_create(pl-d3d11): %s",
             mpv_error_string(err));
        return false;
    }
    mpv_render_context_set_update_callback(s->rctx, on_render_update, s);
    return true;
}

void render_frame(arca_render_session *s, bool force_redraw) {
    uint64_t flags = mpv_render_context_update(s->rctx);
    bool have_frame = (flags & MPV_RENDER_UPDATE_FRAME) != 0;
    if (!have_frame && !force_redraw)
        return;

    if (s->resize_pending.exchange(false)) {
        int w = s->pending_w.load(), h = s->pending_h.load();
        if (w > 0 && h > 0 && (w != s->width || h != s->height)) {
            // Legal even though we wrap the backbuffer: P5b.1 guarantees the
            // engine drops its wrap before render returns, and our own
            // backbuffer/RTV references are frame-scoped.
            HRESULT hr = s->swapchain->ResizeBuffers(0, w, h,
                                                     DXGI_FORMAT_UNKNOWN, 0);
            if (FAILED(hr)) {
                logf(s->engine, "[arca-render] ResizeBuffers(%dx%d) failed: "
                                "0x%08lx", w, h, hr);
            } else {
                s->width = w;
                s->height = h;
                query_display_hdr(s);
            }
        }
    }
    if (s->scale_pending.exchange(false))
        apply_scale(s);

    ComPtr<ID3D11Texture2D> backbuffer;
    if (FAILED(s->swapchain->GetBuffer(0, IID_PPV_ARGS(&backbuffer))))
        return;

    // The core owns border clearing (engine runs --border-background=none;
    // see header note). Transient RTV: released at frame scope.
    ComPtr<ID3D11RenderTargetView> rtv;
    if (SUCCEEDED(s->device->CreateRenderTargetView(backbuffer.Get(), nullptr,
                                                    &rtv))) {
        const float black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
        s->context->ClearRenderTargetView(rtv.Get(), black);
    }

    mpv_d3d11_tex tex{};
    tex.tex = backbuffer.Get();
    tex.w = s->width;
    tex.h = s->height;

    mpv_render_param_target_colorspace tc{};
    tc.primaries = MPV_COLOR_PRIMARIES_BT_2020;
    tc.transfer = MPV_COLOR_TRANSFER_PQ;
    tc.hdr.max_luma = s->disp_max_nits.load();
    tc.hdr.min_luma = s->disp_min_nits.load();

    mpv_render_param params[4];
    int n = 0;
    params[n++] = {MPV_RENDER_PARAM_D3D11_TEX, &tex};
    if (s->hdr_active.load())
        params[n++] = {MPV_RENDER_PARAM_TARGET_COLORSPACE, &tc};
    params[n] = {MPV_RENDER_PARAM_INVALID, nullptr};

    int err = mpv_render_context_render(s->rctx, params);
    if (err < 0) {
        logf(s->engine, "[arca-render] render: %s", mpv_error_string(err));
        return;
    }

    s->swapchain->Present(1, 0);
}

void render_thread(arca_render_session *s, std::promise<bool> ready) {
    bool ok = create_device(s) && create_swapchain(s);
    if (ok) {
        s->hdr_active.store(negotiate_colorspace(s));
        query_display_hdr(s);
        ok = create_render_context(s);
    }
    ready.set_value(ok);
    if (!ok)
        return;

    while (!s->quit.load()) {
        {
            std::unique_lock<std::mutex> lock(s->mutex);
            s->cv.wait_for(lock, std::chrono::milliseconds(100),
                           [s] { return s->wake; });
            s->wake = false;
        }
        if (s->quit.load())
            break;
        render_frame(s, s->resize_pending.load());
    }

    // The render context was created on this thread; free it here too,
    // before the graphics objects it renders into go away.
    mpv_render_context_free(s->rctx);
    s->rctx = nullptr;
}

void destroy_session(arca_render_session *s) {
    s->quit.store(true);
    poke(s);
    if (s->thread.joinable())
        s->thread.join();
    delete s;
}

} // namespace

extern "C" {

namespace {

arca_render_session *create_session(arca_engine *engine, HWND hwnd,
                                    IUnknown *panel_native,
                                    int width, int height,
                                    float scale_x, float scale_y) {
    {
        std::lock_guard<std::mutex> lock(engine->session_mutex);
        if (engine->session) {
            logf(engine, "[arca-render] engine already has a render session");
            return nullptr;
        }
    }

    auto *s = new (std::nothrow) arca_render_session();
    if (!s)
        return nullptr;
    s->engine = engine;
    s->hwnd = hwnd;
    s->panel_native = panel_native;
    s->panel_mode = panel_native != nullptr;
    s->width = width;
    s->height = height;
    s->scale_x.store(scale_x);
    s->scale_y.store(scale_y);

    std::promise<bool> ready;
    std::future<bool> ready_f = ready.get_future();
    s->thread = std::thread(render_thread, s, std::move(ready));
    if (!ready_f.get()) {
        destroy_session(s);
        return nullptr;
    }

    // Attach the composition swapchain to the panel on the caller's (UI)
    // thread, as SwapChainPanel interop requires.
    if (s->panel_mode) {
        ComPtr<IArcaSwapChainPanelNative> native;
        HRESULT hr = s->panel_native.As(&native);
        if (SUCCEEDED(hr))
            hr = native->SetSwapChain(s->swapchain.Get());
        if (FAILED(hr)) {
            logf(engine, "[arca-render] SetSwapChain on panel failed: 0x%08lx", hr);
            destroy_session(s);
            return nullptr;
        }
    }

    std::lock_guard<std::mutex> lock(engine->session_mutex);
    engine->session = s;
    return s;
}

} // namespace

arca_render_session *arca_render_create_hwnd(arca_engine *engine, void *hwnd,
                                             int width, int height) {
    if (!engine || !hwnd || width <= 0 || height <= 0)
        return nullptr;
    return create_session(engine, static_cast<HWND>(hwnd), nullptr,
                          width, height, 1.0f, 1.0f);
}

arca_render_session *arca_render_create_panel(arca_engine *engine,
                                              void *panel_native,
                                              void *hwnd_for_monitor,
                                              int width, int height,
                                              float scale_x, float scale_y) {
    if (!engine || !panel_native || width <= 0 || height <= 0 ||
        scale_x <= 0.0f || scale_y <= 0.0f)
        return nullptr;
    return create_session(engine, static_cast<HWND>(hwnd_for_monitor),
                          static_cast<IUnknown *>(panel_native),
                          width, height, scale_x, scale_y);
}

void arca_render_set_scale(arca_render_session *session,
                           float scale_x, float scale_y) {
    if (!session || scale_x <= 0.0f || scale_y <= 0.0f)
        return;
    session->scale_x.store(scale_x);
    session->scale_y.store(scale_y);
    session->scale_pending.store(true);
    poke(session);
}

void arca_render_resize(arca_render_session *session, int width, int height) {
    if (!session || width <= 0 || height <= 0)
        return;
    session->pending_w.store(width);
    session->pending_h.store(height);
    session->resize_pending.store(true);
    poke(session);
}

arca_status arca_render_get_target_info(arca_render_session *session,
                                        arca_render_target_info *out) {
    if (!session || !out)
        return ARCA_ERR_INVALID_ARG;
    out->hdr_active = session->hdr_active.load();
    out->display_max_nits = session->disp_max_nits.load();
    out->display_min_nits = session->disp_min_nits.load();
    out->width = session->width;
    out->height = session->height;
    return ARCA_OK;
}

void arca_render_destroy(arca_render_session *session) {
    if (!session)
        return;
    arca_engine *engine = session->engine;
    // No surface means no playback: unload first, otherwise the engine's VO
    // fails mid-file when its render context disappears.
    const char *stop_cmd[] = {"stop", nullptr};
    mpv_command(engine->mpv, stop_cmd);
    {
        std::lock_guard<std::mutex> lock(engine->session_mutex);
        if (engine->session == session)
            engine->session = nullptr;
    }
    destroy_session(session);
}

} // extern "C"

void arca_render_detach_for_engine_destroy(arca_engine *engine) {
    arca_render_session *s;
    {
        std::lock_guard<std::mutex> lock(engine->session_mutex);
        s = engine->session;
        engine->session = nullptr;
    }
    if (s)
        destroy_session(s);
}
