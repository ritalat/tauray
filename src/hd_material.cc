#include "hd_material.hh"
#include "hd_render_delegate.hh"

#include "pxr/imaging/hd/material.h"
#include "pxr/imaging/hd/sceneDelegate.h"

#include <mutex>
#include <iostream>

namespace tr
{

HdTaurayMaterial::HdTaurayMaterial(pxr::SdfPath const& id)
:   pxr::HdMaterial(id)
{
}

HdTaurayMaterial::~HdTaurayMaterial()
{
}

pxr::HdDirtyBits HdTaurayMaterial::GetInitialDirtyBitsMask() const
{
    return pxr::HdMaterial::AllDirty;
}

void HdTaurayMaterial::Sync(
    pxr::HdSceneDelegate* sceneDelegate,
    pxr::HdRenderParam* renderParam,
    pxr::HdDirtyBits* dirtyBits
)
{
#ifdef PROJECT_DEBUG
    HdTaurayRenderParam *param = static_cast<HdTaurayRenderParam*>(renderParam);
    std::lock_guard<std::mutex> lock(*(param->mtx));
#endif
    TR_DBG("Sync Material id=", GetId().GetText());

    _mat.albedo_factor = { 0.5f, 0.5f, 0.5f, 1.0f };
    _mat.albedo_tex.first = nullptr;
    _mat.metallic_factor = 0.5f;
    _mat.roughness_factor = 0.5f;
    _mat.metallic_roughness_tex.first = nullptr;
    _mat.normal_factor = 1.0f;
    _mat.normal_tex.first = nullptr;
    _mat.ior = 1.45f;
    _mat.emission_factor = { 0.0f, 0.0f, 0.0f };
    _mat.emission_tex.first = nullptr;
    _mat.double_sided = false;
    _mat.name = GetId().GetAsString();

    // We only support a UsdPreviewSurface node since it mostly maps to gltf
    // Only tested with Blender exports
    pxr::VtValue val = sceneDelegate->GetMaterialResource(GetId());
    if(val.IsHolding<pxr::HdMaterialNetworkMap>())
    {
        pxr::HdMaterialNetworkMap network = val.Get<pxr::HdMaterialNetworkMap>();
        pxr::HdMaterialNetwork2 network2 = pxr::HdConvertToHdMaterialNetwork2(network);
        for(auto const&[nodeName, node] : network2.nodes)
        {
            if(node.nodeTypeId == pxr::TfToken("UsdPreviewSurface"))
                _LoadPreviewSurfaceParams(node);

            if(node.nodeTypeId == pxr::TfToken("UsdUVTexture"))
                _LoadTexture(node, nodeName, renderParam);
        }
    }

    if(!_diffuseNode.empty())
        _mat.albedo_tex.first = _tex[_diffuseNode].tex;

    if(!_normalNode.empty())
        _mat.normal_tex.first = _tex[_normalNode].tex;

    if(!_metallicNode.empty() && _metallicNode == _roughnessNode)
        _mat.metallic_roughness_tex.first = _tex[_metallicNode].tex;

    if(!_emissionNode.empty())
        _mat.emission_tex.first = _tex[_emissionNode].tex;

    *dirtyBits &= ~pxr::HdMaterial::AllDirty;
}

void HdTaurayMaterial::_LoadPreviewSurfaceParams(pxr::HdMaterialNode2 const& node)
{
    for(auto const&[paramName, param] : node.parameters)
    {
        if(paramName == pxr::TfToken("diffuseColor"))
        {
            if(param.IsHolding<pxr::GfVec3f>())
            {
                pxr::GfVec3f v = param.Get<pxr::GfVec3f>();
                _mat.albedo_factor = {v[0], v[1], v[2], 1.0f};
            }
        }
        else if(paramName == pxr::TfToken("metallic"))
        {
            if(param.IsHolding<float>())
            {
                float v = param.Get<float>();
                _mat.metallic_factor = v;
            }
        }
        else if(paramName == pxr::TfToken("roughness"))
        {
            if(param.IsHolding<float>())
            {
                float v = param.Get<float>();
                _mat.roughness_factor = v;
            }
        }
        else if(paramName == pxr::TfToken("emissiveColor"))
        {
            if(param.IsHolding<pxr::GfVec3f>())
            {
                pxr::GfVec3f v = param.Get<pxr::GfVec3f>();
                _mat.emission_factor = {v[0], v[1], v[2]};
            }
        }
        else if(paramName == pxr::TfToken("ior"))
        {
            if(param.IsHolding<float>())
            {
                float v = param.Get<float>();
                _mat.ior = v;
            }
        }
        // UsdPreviewSurface only has opacity, which Blender maps to alpha
        // Set alpha to 1-transmission for this to work :)
        else if(paramName == pxr::TfToken("opacity"))
        {
            if(param.IsHolding<float>())
            {
                float v = param.Get<float>();
                _mat.transmittance = std::clamp(1.0f - v, 0.0f, 1.0f);
            }
        }
    }

    for(auto const&[connName, conn] : node.inputConnections)
    {
        if(connName == pxr::TfToken("diffuseColor"))
        {
            _diffuseNode = conn[0].upstreamNode.GetAsString();
        }
        else if(connName == pxr::TfToken("normal"))
        {
            _normalNode = conn[0].upstreamNode.GetAsString();
        }
        else if(connName == pxr::TfToken("metallic"))
        {
            _metallicNode = conn[0].upstreamNode.GetAsString();
        }
        else if(connName == pxr::TfToken("roughness"))
        {
            _roughnessNode = conn[0].upstreamNode.GetAsString();
        }
        else if(connName == pxr::TfToken("emissiveColor"))
        {
            _emissionNode = conn[0].upstreamNode.GetAsString();
        }
    }
}

void HdTaurayMaterial::_LoadTexture(
    pxr::HdMaterialNode2 const& node,
    pxr::SdfPath const& nodeName,
    pxr::HdRenderParam* renderParam
)
{
    HdTaurayRenderParam *param = static_cast<HdTaurayRenderParam*>(renderParam);
#ifndef PROJECT_DEBUG
    std::lock_guard<std::mutex> lock(*(param->mtx));
#endif
    device_mask dev = param->dev;
    scene_assets* md = param->md;

    TextureData t;

    for(auto const&[paramName, param] : node.parameters)
    {
        if(paramName == pxr::TfToken("file"))
        {
            if(param.IsHolding<pxr::SdfAssetPath>())
            {
                // The path should really be resolved into an ArAsset
                // via ArGetResolver for USDZ suppport...
                // Just use GetResolvePath directly for loose files for now
                pxr::SdfAssetPath v = param.Get<pxr::SdfAssetPath>();
                TR_DBG("Texture asset=", v.GetAssetPath());
                md->textures.emplace_back(new texture(dev, v.GetResolvedPath()));
                t.tex = md->textures.back().get();
            }
        }
        else if(paramName == pxr::TfToken("wrapS"))
        {
            if(param.IsHolding<pxr::TfToken>())
            {
                pxr::TfToken v = param.Get<pxr::TfToken>();
                TR_DBG("Wrap mode S=", v.GetText());
                t.wraps = v.GetString();
            }
        }
        else if(paramName == pxr::TfToken("wrapT"))
        {
            if(param.IsHolding<pxr::TfToken>())
            {
                pxr::TfToken v = param.Get<pxr::TfToken>();
                TR_DBG("Wrap mode T=", v.GetText());
                t.wrapt = v.GetString();
            }
        }
        // TODO: Further texture parameters
        else if(paramName == pxr::TfToken("sourceColorSpace"))
        {
            TR_DBG("Unhandled texture color space");
        }
        else if(paramName == pxr::TfToken("bias"))
        {
            TR_DBG("Unhandled texture bias");
        }
        else if(paramName == pxr::TfToken("scale"))
        {
            TR_DBG("Unhandled texture scale");
        }
    }

    _tex[nodeName.GetAsString()] = t;
}

material HdTaurayMaterial::GetMaterial() const
{
    return _mat;
}

void HdTaurayMaterial::Finalize(pxr::HdRenderParam* renderParam)
{
    // FIXME: Cleanup
}

}
