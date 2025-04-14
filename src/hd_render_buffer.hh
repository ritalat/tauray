#ifndef TAURAY_HD_RENDER_BUFFER_HH
#define TAURAY_HD_RENDER_BUFFER_HH

#include "headless.hh"

#include "pxr/pxr.h"
#include "pxr/imaging/hd/renderBuffer.h"

namespace tr
{

class HdTaurayRenderBuffer : public pxr::HdRenderBuffer
{
public:
    HdTaurayRenderBuffer(pxr::SdfPath const& id, headless* ctx);
    ~HdTaurayRenderBuffer() override;

    void Sync(
        pxr::HdSceneDelegate* sceneDelegate,
        pxr::HdRenderParam* renderParam,
        pxr::HdDirtyBits* dirtyBits
    ) override;

    void Finalize(pxr::HdRenderParam* renderParam) override;

    bool Allocate(
        pxr::GfVec3i const& dimensions,
        pxr::HdFormat format,
        bool multiSampled
    ) override;

    void Recreate();

    bool Dirty();

    unsigned int GetWidth() const override { return _width; }

    unsigned int GetHeight() const override { return _height; }

    unsigned int GetDepth() const override { return 1; }

    pxr::HdFormat GetFormat() const override { return _format; }

    bool IsMultiSampled() const override { return false; }

    void* Map() override;

    void Unmap() override;

    bool IsMapped() const override;

    bool IsConverged() const override;

    void Resolve() override {};

private:
    void _Deallocate() override;

    headless* _ctx;
    bool _dirty;
    unsigned int _width;
    unsigned int _height;
    pxr::HdFormat _format;
};

}

#endif
