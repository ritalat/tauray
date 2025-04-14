#include "hd_render_buffer.hh"

#include "headless.hh"
#include "tauray.hh"

#include "pxr/base/gf/vec3f.h"

#include <iostream>

namespace tr
{

// FIXME/HACK: Only support one render buffer per render pass
//  and copy the last swapchain image from headless context.
//  This works with the common case where hydra client simply
//  wants to render a scene and it's enough to support usdview.
//  Create a new context type for more complex image creation.

HdTaurayRenderBuffer::HdTaurayRenderBuffer(pxr::SdfPath const& id, headless* ctx)
:   pxr::HdRenderBuffer(id),
    _ctx(ctx),
    _dirty(true),
    _width(800),
    _height(600),
    _format(pxr::HdFormatInvalid)
{
}

HdTaurayRenderBuffer::~HdTaurayRenderBuffer() = default;

void HdTaurayRenderBuffer::Sync(
    pxr::HdSceneDelegate* sceneDelegate,
    pxr::HdRenderParam* renderParam,
    pxr::HdDirtyBits* dirtyBits)
{
    TR_DBG("Sync Render Buffer id=", GetId().GetText());
    pxr::HdRenderBuffer::Sync(sceneDelegate, renderParam, dirtyBits);
}

void HdTaurayRenderBuffer::Finalize(pxr::HdRenderParam* renderParam)
{
    pxr::HdRenderBuffer::Finalize(renderParam);
}

void HdTaurayRenderBuffer::_Deallocate()
{
    _dirty = true;
    _width = 800;
    _height = 600;
    _format = pxr::HdFormatInvalid;
}

bool HdTaurayRenderBuffer::Allocate(
    pxr::GfVec3i const& dims,
    pxr::HdFormat format,
    bool multiSampled
)
{
    _Deallocate();

    TR_DBG("Allocate new Render Buffer dims=",
           dims[0], ",", dims[1], " format=",
           pxr::TfEnum::GetName(format));

    if(dims[2] != 1 || multiSampled || format != pxr::HdFormatFloat32Vec4)
    {
        TR_DBG("Failed to allocate Render Buffer with depth=",
               dims[2], ", multisample= ", (multiSampled ? "true" : "false"),
               "and format=", format);
        return false;
    }

    _dirty = true;
    _width = dims[0];
    _height = dims[1];
    _format = format;

    return true;
}

void HdTaurayRenderBuffer::Recreate()
{
    _ctx->recreate_images({_width, _height});
    _dirty = false;
}

bool HdTaurayRenderBuffer::Dirty()
{
    return _dirty;
}

void* HdTaurayRenderBuffer::Map()
{
    if(_dirty)
        std::runtime_error("Trying to map dirty Render Buffer");

    TR_DBG("Mapping Render Buffer");
    return _ctx->map_hydra_image();
}

void HdTaurayRenderBuffer::Unmap()
{
    TR_DBG("Unmapping Render Buffer");
    _ctx->unmap_hydra_image();
}

bool HdTaurayRenderBuffer::IsMapped() const
{
    TR_DBG("Testing Render Buffer mapping: ", _ctx->hydra_image_mapped());
    return _ctx->hydra_image_mapped();
}

bool HdTaurayRenderBuffer::IsConverged() const
{
    TR_DBG("Testing Render Buffer converged: ", _ctx->hydra_image_converged());
    return _ctx->hydra_image_converged();
}

}
