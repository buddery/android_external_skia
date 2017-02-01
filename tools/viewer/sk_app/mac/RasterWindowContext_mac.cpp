
/*
 * Copyright 2016 Google Inc.
 *
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "../GLWindowContext.h"
#include "SkCanvas.h"
#include "SkColorFilter.h"
#include "sk_tool_utils.h"
#include "WindowContextFactory_mac.h"

#include "SDL.h"

#include <OpenGL/gl.h>

using sk_app::DisplayParams;
using sk_app::window_context_factory::MacWindowInfo;
using sk_app::GLWindowContext;

namespace {

// We use SDL to support Mac windowing mainly for convenience's sake. However, it
// does not allow us to support a purely raster backend because we have no hooks into
// the NSWindow's drawRect: method. Hence we use GL to handle the update. Should we
// want to avoid this, we will probably need to write our own windowing backend.

class RasterWindowContext_mac : public GLWindowContext {
public:
    RasterWindowContext_mac(const MacWindowInfo&, const DisplayParams&);

    ~RasterWindowContext_mac() override;

    sk_sp<SkSurface> getBackbufferSurface() override;

    void onSwapBuffers() override;

    void onInitializeContext() override;
    void onDestroyContext() override;

private:
    SDL_Window*   fWindow;
    SDL_GLContext fGLContext;
    sk_sp<SkSurface> fBackbufferSurface;

    typedef GLWindowContext INHERITED;
};

RasterWindowContext_mac::RasterWindowContext_mac(const MacWindowInfo& info,
                                                 const DisplayParams& params)
    : INHERITED(params)
    , fWindow(info.fWindow)
    , fGLContext(nullptr) {

    // any config code here (particularly for msaa)?

    this->initializeContext();
}

RasterWindowContext_mac::~RasterWindowContext_mac() {
    this->destroyContext();
}

void RasterWindowContext_mac::onInitializeContext() {
    SkASSERT(fWindow);

    fGLContext = SDL_GL_CreateContext(fWindow);
    if (!fGLContext) {
        SkDebugf("%s\n", SDL_GetError());
        return;
    }

    if (0 == SDL_GL_MakeCurrent(fWindow, fGLContext)) {
        glClearStencil(0);
        glClearColor(0, 0, 0, 0);
        glStencilMask(0xffffffff);
        glClear(GL_STENCIL_BUFFER_BIT | GL_COLOR_BUFFER_BIT);

        int redBits, greenBits, blueBits;
        SDL_GL_GetAttribute(SDL_GL_RED_SIZE, &redBits);
        SDL_GL_GetAttribute(SDL_GL_GREEN_SIZE, &greenBits);
        SDL_GL_GetAttribute(SDL_GL_BLUE_SIZE, &blueBits);
        fColorBits = redBits + greenBits + blueBits;

        SDL_GL_GetAttribute(SDL_GL_STENCIL_SIZE, &fStencilBits);
        SDL_GL_GetAttribute(SDL_GL_MULTISAMPLESAMPLES, &fSampleCount);

        SDL_GetWindowSize(fWindow, &fWidth, &fHeight);
        glViewport(0, 0, fWidth, fHeight);
    } else {
        SkDebugf("MakeCurrent failed: %s\n", SDL_GetError());
    }

    // make the offscreen image
    SkImageInfo info = SkImageInfo::Make(fWidth, fHeight, fDisplayParams.fColorType,
                                         kPremul_SkAlphaType, fDisplayParams.fColorSpace);
    fBackbufferSurface = SkSurface::MakeRaster(info);
}

void RasterWindowContext_mac::onDestroyContext() {
    if (!fWindow || !fGLContext) {
        return;
    }
    fBackbufferSurface.reset(nullptr);
    SDL_GL_DeleteContext(fGLContext);
    fGLContext = nullptr;
}

sk_sp<SkSurface> RasterWindowContext_mac::getBackbufferSurface() { return fBackbufferSurface; }

void RasterWindowContext_mac::onSwapBuffers() {
    if (fWindow && fGLContext) {
        // We made/have an off-screen surface. Get the contents as an SkImage:
        sk_sp<SkImage> snapshot = fBackbufferSurface->makeImageSnapshot();
        
        // With ten-bit output, we need to manually apply the gamma of the output device
        // (unless we're in non-gamma correct mode, in which case our data is already
        // fake-sRGB, like we're expected to put in the 10-bit buffer):
        bool doGamma = (fActualColorBits == 30) &&
                       (fDisplayParams.fColorSpace != nullptr ||
                        kRGBA_F16_SkColorType == fDisplayParams.fColorType);
        SkPaint gammaPaint;
        gammaPaint.setBlendMode(SkBlendMode::kSrc);
        if (doGamma) {
            gammaPaint.setColorFilter(sk_tool_utils::MakeLinearToSRGBColorFilter());
        }

        sk_sp<SkSurface> gpuSurface = INHERITED::getBackbufferSurface();
        SkCanvas* gpuCanvas = gpuSurface->getCanvas();
        gpuCanvas->drawImage(snapshot, 0, 0, &gammaPaint);
        gpuCanvas->flush();

        SDL_GL_SwapWindow(fWindow);
    }
}

}  // anonymous namespace

namespace sk_app {
namespace window_context_factory {

WindowContext* NewRasterForMac(const MacWindowInfo& info, const DisplayParams& params) {
    WindowContext* ctx = new RasterWindowContext_mac(info, params);
    if (!ctx->isValid()) {
        delete ctx;
        return nullptr;
    }
    return ctx;
}

}  // namespace window_context_factory
}  // namespace sk_app
