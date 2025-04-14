#ifndef TAURAY_HD_RENDERER_PLUGIN_HH
#define TAURAY_HD_RENDERER_PLUGIN_HH

#include "pxr/pxr.h"
#include "pxr/imaging/hd/rendererPlugin.h"

namespace tr
{

class HdTaurayRendererPlugin final : public pxr::HdRendererPlugin
{
public:
    HdTaurayRendererPlugin() = default;
    virtual ~HdTaurayRendererPlugin() = default;

    virtual pxr::HdRenderDelegate* CreateRenderDelegate() override;

    virtual pxr::HdRenderDelegate* CreateRenderDelegate(
        pxr::HdRenderSettingsMap const& settingsMap
    ) override;

    virtual void DeleteRenderDelegate(
        pxr::HdRenderDelegate* renderDelegate
    ) override;

    virtual bool IsSupported(bool gpuEnabled = true) const override;

private:
    HdTaurayRendererPlugin(const HdTaurayRendererPlugin&) = delete;
    HdTaurayRendererPlugin& operator =(const HdTaurayRendererPlugin&) = delete;
};

}

#endif
