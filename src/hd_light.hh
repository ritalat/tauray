#ifndef TAURAY_HD_LIGHT_HH
#define TAURAY_HD_LIGHT_HH

#include "monkeroecs.hh"

#include "pxr/imaging/hd/light.h"

namespace tr
{

class HdTaurayLight final : public pxr::HdLight
{
public:
    HdTaurayLight(pxr::SdfPath const& id, pxr::TfToken const& lightType);
    ~HdTaurayLight();

    pxr::HdDirtyBits GetInitialDirtyBitsMask() const override;

    void Sync(
        pxr::HdSceneDelegate* sceneDelegate,
        pxr::HdRenderParam* renderParam,
        pxr::HdDirtyBits* dirtyBits
    ) override;

    void Finalize(pxr::HdRenderParam* renderParam) override;

private:
    pxr::TfToken _type;
    monkero::entity _id;
};

}

#endif
