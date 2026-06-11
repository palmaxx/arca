// arca_render.h — video render session (data plane).
//
// The core owns the D3D11 device, the swapchain, HDR negotiation, and a
// dedicated render thread; shells hand in a surface and never touch frames.
// Call sequence per frame (proven in the mpv fork's Phase-5a host):
//   GetBuffer(0) -> mpv_render_context_render(D3D11_TEX + TARGET_COLORSPACE)
//   -> Present(1, 0)
// HDR contract (fork Phase-5 findings): the target colorspace passed to the
// renderer carries the *display's* peak luminance (IDXGIOutput6::GetDesc1),
// not the content's mastering peak.

#ifndef ARCA_RENDER_H
#define ARCA_RENDER_H

#include "arca.h"
#include "arca_engine.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct arca_render_session arca_render_session;

typedef struct arca_render_target_info {
    bool hdr_active;          // swapchain negotiated BT.2020/PQ (HDR10)
    float display_max_nits;   // display peak luminance (tone-map target)
    float display_min_nits;
    int width;
    int height;
} arca_render_target_info;

// Creates a render session presenting into a Win32 window. One session per
// engine. Returns NULL on failure (details on the engine log).
ARCA_API arca_render_session *arca_render_create_hwnd(arca_engine *engine,
                                                      void *hwnd,
                                                      int width, int height);

// Resize the swapchain (e.g. from WM_SIZE / panel SizeChanged). Safe from
// any thread; applied before the next rendered frame.
ARCA_API void arca_render_resize(arca_render_session *session, int width, int height);

ARCA_API arca_status arca_render_get_target_info(arca_render_session *session,
                                                 arca_render_target_info *out);

// Destroys the session (stops the render thread, frees the engine-side
// render context, releases graphics objects). Must precede engine destroy —
// arca_engine_destroy also does this automatically.
ARCA_API void arca_render_destroy(arca_render_session *session);

#ifdef __cplusplus
}
#endif

#endif // ARCA_RENDER_H
