#ifndef TAURAY_HD_RENDER_DELEGATE_HH
#define TAURAY_HD_RENDER_DELEGATE_HH

#include "tauray.hh"

#include "pxr/pxr.h"
#include "pxr/imaging/hd/renderDelegate.h"
#include "pxr/imaging/hd/resourceRegistry.h"
#include "pxr/base/tf/staticTokens.h"

#include <memory>
#include <mutex>

PXR_NAMESPACE_OPEN_SCOPE
#define HDTAURAY_SETTINGS_TOKENS (accumulate) (samples) (sampleBatch) (rayDepth)
TF_DECLARE_PUBLIC_TOKENS(HdTauraySettingsTokens, HDTAURAY_SETTINGS_TOKENS);
PXR_NAMESPACE_CLOSE_SCOPE

namespace tr
{

class HdTaurayRenderParam final : public pxr::HdRenderParam
{
public:
    HdTaurayRenderParam(
        device_mask device,
        scene_assets* assets,
        monkero::scene* scene,
        std::mutex* scene_mtx
    )
    :   dev(device),
        md(assets),
        s(scene),
        mtx(scene_mtx)
    {
    }

    device_mask dev;
    scene_assets* md;
    monkero::scene* s;
    std::mutex* mtx;
};

class HdTaurayRenderDelegate final : public pxr::HdRenderDelegate
{
public:
    HdTaurayRenderDelegate();

    HdTaurayRenderDelegate(pxr::HdRenderSettingsMap const& settingsMap);

    virtual ~HdTaurayRenderDelegate();

    const pxr::TfTokenVector &GetSupportedRprimTypes() const override;
    const pxr::TfTokenVector &GetSupportedSprimTypes() const override;
    const pxr::TfTokenVector &GetSupportedBprimTypes() const override;

    pxr::HdResourceRegistrySharedPtr GetResourceRegistry() const override;

    pxr::HdRenderPassSharedPtr CreateRenderPass(
        pxr::HdRenderIndex *index,
        pxr::HdRprimCollection const& collection
    ) override;

    pxr::HdInstancer* CreateInstancer(
        pxr::HdSceneDelegate* delegate,
        pxr::SdfPath const& id
    ) override;
    void DestroyInstancer(pxr::HdInstancer* instancer) override;

    pxr::HdRprim* CreateRprim(
        pxr::TfToken const& typeId,
        pxr::SdfPath const& rprimId
    ) override;
    void DestroyRprim(pxr::HdRprim* rPrim) override;

    pxr::HdSprim* CreateSprim(
        pxr::TfToken const& typeId,
        pxr::SdfPath const& sprimId
    ) override;
    pxr::HdSprim* CreateFallbackSprim(pxr::TfToken const& typeId) override;
    void DestroySprim(pxr::HdSprim* sprim) override;

    pxr::HdBprim* CreateBprim(
        pxr::TfToken const& typeId,
        pxr::SdfPath const& bprimId
    ) override;
    pxr::HdBprim* CreateFallbackBprim(pxr::TfToken const& typeId) override;
    void DestroyBprim(pxr::HdBprim* bprim) override;

    void CommitResources(pxr::HdChangeTracker* tracker) override;

    pxr::HdRenderParam* GetRenderParam() const override;

    pxr::HdAovDescriptor GetDefaultAovDescriptor(
        pxr::TfToken const& name
    ) const override;

    pxr::HdRenderSettingDescriptorList GetRenderSettingDescriptors() const override;

private:
    static const pxr::TfTokenVector SUPPORTED_RPRIM_TYPES;
    static const pxr::TfTokenVector SUPPORTED_SPRIM_TYPES;
    static const pxr::TfTokenVector SUPPORTED_BPRIM_TYPES;

    void _Initialize();

    std::unique_ptr<headless> _ctx;
    scene_data _sd;
    scene_assets _md;
    std::mutex _scene_mtx;
    options _opt;
    std::unique_ptr<HdTaurayRenderParam> _renderParam;
    pxr::HdResourceRegistrySharedPtr _resourceRegistry;
    pxr::HdRenderSettingDescriptorList _settingDescriptors;

    HdTaurayRenderDelegate(const HdTaurayRenderDelegate &) = delete;
    HdTaurayRenderDelegate& operator =(const HdTaurayRenderDelegate &) = delete;
};

}

#endif
