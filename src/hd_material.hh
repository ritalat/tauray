#ifndef TAURAY_HD_MATERIAL_HH
#define TAURAY_HD_MATERIAL_HH

#include "material.hh"

#include "pxr/imaging/hd/material.h"

#include <string>
#include <unordered_map>

namespace tr
{

class HdTaurayMaterial final : public pxr::HdMaterial
{
public:
    HdTaurayMaterial(pxr::SdfPath const& id);
    ~HdTaurayMaterial();

    pxr::HdDirtyBits GetInitialDirtyBitsMask() const override;

    void Sync(
        pxr::HdSceneDelegate* sceneDelegate,
        pxr::HdRenderParam* renderParam,
        pxr::HdDirtyBits* dirtyBits
    ) override;

    material GetMaterial() const;

    void Finalize(pxr::HdRenderParam* renderParam) override;

private:
    void _LoadPreviewSurfaceParams(pxr::HdMaterialNode2 const& node);

    void _LoadTexture(
        pxr::HdMaterialNode2 const& node,
        pxr::SdfPath const& nodeName,
        pxr::HdRenderParam* renderParam
    );

    material _mat;
    std::string _diffuseNode;
    std::string _normalNode;
    std::string _metallicNode;
    std::string _roughnessNode;
    std::string _emissionNode;

    struct TextureData {
        texture* tex;
        std::string wraps;
        std::string wrapt;
    };
    std::unordered_map<std::string, TextureData> _tex;
};

}

#endif
