#include "hd_render_delegate.hh"
#include "hd_config.hh"
#include "hd_light.hh"
#include "hd_material.hh"
#include "hd_mesh.hh"
#include "hd_render_buffer.hh"
#include "hd_render_pass.hh"

#include "tauray.hh"

#include "pxr/imaging/hd/camera.h"

PXR_NAMESPACE_OPEN_SCOPE
TF_DEFINE_PUBLIC_TOKENS(HdTauraySettingsTokens, HDTAURAY_SETTINGS_TOKENS);
PXR_NAMESPACE_CLOSE_SCOPE

namespace tr
{

const pxr::TfTokenVector HdTaurayRenderDelegate::SUPPORTED_RPRIM_TYPES =
{
    pxr::HdPrimTypeTokens->mesh,
};

const pxr::TfTokenVector HdTaurayRenderDelegate::SUPPORTED_SPRIM_TYPES =
{
    pxr::HdPrimTypeTokens->camera,
    pxr::HdPrimTypeTokens->distantLight,
    pxr::HdPrimTypeTokens->sphereLight,
    pxr::HdPrimTypeTokens->material
};

const pxr::TfTokenVector HdTaurayRenderDelegate::SUPPORTED_BPRIM_TYPES =
{
    pxr::HdPrimTypeTokens->renderBuffer,
};

HdTaurayRenderDelegate::HdTaurayRenderDelegate()
:   pxr::HdRenderDelegate()
{
    _Initialize();
}

HdTaurayRenderDelegate::HdTaurayRenderDelegate(
    pxr::HdRenderSettingsMap const& settingsMap
)
    : pxr::HdRenderDelegate(settingsMap)
{
    _Initialize();
}

void HdTaurayRenderDelegate::_Initialize()
{
    TR_DBG("Creating RenderDelegate");

    _resourceRegistry = std::make_shared<pxr::HdResourceRegistry>();

    _settingDescriptors.push_back(
        {
            "Accumulate",
            pxr::HdTauraySettingsTokens->accumulate,
            pxr::VtValue(HdTaurayConfig::GetInstance().accumulate)
        }
    );
    _settingDescriptors.push_back(
        {
            "Target samples per pixel",
            pxr::HdTauraySettingsTokens->samples,
            pxr::VtValue(HdTaurayConfig::GetInstance().samples)
        }
    );
    _settingDescriptors.push_back(
        {
            "Samples per batch",
            pxr::HdTauraySettingsTokens->sampleBatch,
            pxr::VtValue(HdTaurayConfig::GetInstance().sampleBatch)
        }
    );
    _settingDescriptors.push_back(
        {
            "Maximum ray depth",
            pxr::HdTauraySettingsTokens->rayDepth,
            pxr::VtValue(HdTaurayConfig::GetInstance().rayDepth)
        }
    );
    _PopulateDefaultSettings(_settingDescriptors);

    // Must be true or Hydra might think we got stuck
    _opt.accumulation = 1; //HdTaurayConfig::GetInstance().accumulate;
    _opt.samples_per_pixel = HdTaurayConfig::GetInstance().sampleBatch;
    _opt.max_ray_depth = HdTaurayConfig::GetInstance().rayDepth;
    _opt.regularization = 0.2;
    _opt.sampler = rt_stage::sampler_type::UNIFORM_RANDOM;

    headless::options hd_opt;
    hd_opt.max_timestamps = 128;
    hd_opt.output_prefix = "hdtauray";
    hd_opt.output_file_type = headless::HYDRA;
    _ctx.reset(new headless(hd_opt));
    _ctx->set_accumulating(_opt.accumulation);

    _sd.s.reset(new scene);

    _renderParam.reset(new HdTaurayRenderParam(
        device_mask::all(*_ctx),
        &_md, _sd.s.get(),
        &_scene_mtx
    ));
}

HdTaurayRenderDelegate::~HdTaurayRenderDelegate()
{
    _resourceRegistry.reset();
    TR_DBG("Destroying RenderDelegate");
}

pxr::TfTokenVector const& HdTaurayRenderDelegate::GetSupportedRprimTypes() const
{
    return SUPPORTED_RPRIM_TYPES;
}

pxr::TfTokenVector const& HdTaurayRenderDelegate::GetSupportedSprimTypes() const
{
    return SUPPORTED_SPRIM_TYPES;
}

pxr::TfTokenVector const& HdTaurayRenderDelegate::GetSupportedBprimTypes() const
{
    return SUPPORTED_BPRIM_TYPES;
}

pxr::HdResourceRegistrySharedPtr HdTaurayRenderDelegate::GetResourceRegistry() const
{
    return _resourceRegistry;
}

void HdTaurayRenderDelegate::CommitResources(pxr::HdChangeTracker* tracker)
{
    TR_DBG("CommitResources RenderDelegate");
}

pxr::HdRenderPassSharedPtr HdTaurayRenderDelegate::CreateRenderPass(
    pxr::HdRenderIndex* index,
    pxr::HdRprimCollection const& collection
)
{
    TR_DBG("Create RenderPass with Collection=", collection.GetName().GetText());

    return pxr::HdRenderPassSharedPtr(new HdTaurayRenderPass(
        index, collection, *_ctx, _sd, _opt
    ));
}

pxr::HdRprim* HdTaurayRenderDelegate::CreateRprim(
    pxr::TfToken const& typeId,
    pxr::SdfPath const& rprimId
)
{
    TR_DBG("Create Rprim type=", typeId.GetText(), " id=", rprimId.GetText());

    if(typeId == pxr::HdPrimTypeTokens->mesh)
    {
        return new HdTaurayMesh(rprimId);
    }
    else
    {
        TR_DBG("Unknown Rprim type=", typeId.GetText(), " id=", rprimId.GetText());
    }
    return nullptr;
}

void HdTaurayRenderDelegate::DestroyRprim(pxr::HdRprim* rPrim)
{
    TR_DBG("Destroy Rprim id=", rPrim->GetId().GetText());
    delete rPrim;
}

pxr::HdSprim* HdTaurayRenderDelegate::CreateSprim(
    pxr::TfToken const& typeId,
    pxr::SdfPath const& sprimId
)
{
    TR_DBG("Create Sprim type=", typeId.GetText(), " id=", sprimId.GetText());

    if(typeId == pxr::HdPrimTypeTokens->camera)
    {
        return new pxr::HdCamera(sprimId);
    }
    else if(
        typeId == pxr::HdPrimTypeTokens->distantLight ||
        typeId == pxr::HdPrimTypeTokens->sphereLight
    ){
        return new HdTaurayLight(sprimId, typeId);
    }
    else if(typeId == pxr::HdPrimTypeTokens->material)
    {
        return new HdTaurayMaterial(sprimId);
    }
    else
    {
        TR_DBG("Unknown Sprim type=", typeId.GetText(), " id=", sprimId.GetText());
    }
    return nullptr;
}

pxr::HdSprim* HdTaurayRenderDelegate::CreateFallbackSprim(
    pxr::TfToken const& typeId
)
{
    TR_DBG("Create Fallback Sprim type=", typeId.GetText());

    if(typeId == pxr::HdPrimTypeTokens->camera)
    {
        return new pxr::HdCamera(pxr::SdfPath::EmptyPath());
    }
    else if(
        typeId == pxr::HdPrimTypeTokens->distantLight ||
        typeId == pxr::HdPrimTypeTokens->sphereLight
    ){
        return new HdTaurayLight(pxr::SdfPath::EmptyPath(), typeId);
    }
    else if(typeId == pxr::HdPrimTypeTokens->material)
    {
        return new HdTaurayMaterial(pxr::SdfPath::EmptyPath());
    }
    else
    {
        TR_DBG("Unknown Fallback Sprim type=", typeId.GetText());
    }
    return nullptr;
}

void HdTaurayRenderDelegate::DestroySprim(pxr::HdSprim* sPrim)
{
    if(sPrim)
        TR_DBG("Destroy Sprim id=", sPrim->GetId().GetText());
    delete sPrim;
}

pxr::HdBprim* HdTaurayRenderDelegate::CreateBprim(
    pxr::TfToken const& typeId,
    pxr::SdfPath const& bprimId
)
{
    TR_DBG("Create Bprim type=", typeId.GetText(), " id=", bprimId.GetText());

    if(typeId == pxr::HdPrimTypeTokens->renderBuffer)
    {
        return new HdTaurayRenderBuffer(bprimId, _ctx.get());
    }
    else
    {
        TR_DBG("Unknown Bprim type=", typeId.GetText(), " id=", bprimId.GetText());
    }
    return nullptr;
}

pxr::HdBprim* HdTaurayRenderDelegate::CreateFallbackBprim(
    pxr::TfToken const& typeId
)
{
    TR_DBG("Create Fallback Bprim type=", typeId.GetText());

    if(typeId == pxr::HdPrimTypeTokens->renderBuffer)
    {
        return new HdTaurayRenderBuffer(pxr::SdfPath::EmptyPath(), _ctx.get());
    }
    else
    {
        TR_DBG("Unknown Fallback Bprim type=", typeId.GetText());
    }
    return nullptr;
}

void HdTaurayRenderDelegate::DestroyBprim(pxr::HdBprim* bPrim)
{
    if (bPrim)
        TR_DBG("Destroy Bprim id=", bPrim->GetId().GetText());
    delete bPrim;
}

pxr::HdInstancer* HdTaurayRenderDelegate::CreateInstancer(
    pxr::HdSceneDelegate* delegate,
    pxr::SdfPath const& id
)
{
    // TODO: Instancer
    TR_DBG("Creating Instancer not supported id=", id.GetText());
    return nullptr;
}

void HdTaurayRenderDelegate::DestroyInstancer(pxr::HdInstancer* instancer)
{
    TR_DBG("Destroy instancer not supported");
}

pxr::HdRenderParam* HdTaurayRenderDelegate::GetRenderParam() const
{
    return _renderParam.get();
}

pxr::HdAovDescriptor HdTaurayRenderDelegate::GetDefaultAovDescriptor(
    pxr::TfToken const& name
) const
{
    if(name == pxr::HdAovTokens->color)
    {
        return pxr::HdAovDescriptor(
            pxr::HdFormatFloat32Vec4,
            false,
            pxr::VtValue(pxr::GfVec4f(0.0f))
        );
    }
    return pxr::HdAovDescriptor();
}

pxr::HdRenderSettingDescriptorList HdTaurayRenderDelegate::GetRenderSettingDescriptors() const
{
    return _settingDescriptors;
}

}
