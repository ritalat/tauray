#include "hd_renderer_plugin.hh"
#include "hd_render_delegate.hh"

#include "pxr/imaging/hd/rendererPluginRegistry.h"

PXR_NAMESPACE_OPEN_SCOPE
TF_REGISTRY_FUNCTION(TfType)
{
    HdRendererPluginRegistry::Define<tr::HdTaurayRendererPlugin>();
}
PXR_NAMESPACE_CLOSE_SCOPE

namespace tr
{

pxr::HdRenderDelegate* HdTaurayRendererPlugin::CreateRenderDelegate()
{
    return new HdTaurayRenderDelegate();
}

pxr::HdRenderDelegate* HdTaurayRendererPlugin::CreateRenderDelegate(
    pxr::HdRenderSettingsMap const& settingsMap
)
{
    return new HdTaurayRenderDelegate(settingsMap);
}

void HdTaurayRendererPlugin::DeleteRenderDelegate(pxr::HdRenderDelegate* renderDelegate)
{
    delete renderDelegate;
}

bool HdTaurayRendererPlugin::IsSupported(bool /* gpuEnabled */) const
{
    // TODO: Check vulkan capabilities?
    return true;
}

}
