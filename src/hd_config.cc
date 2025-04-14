#include "hd_config.hh"

#include "pxr/base/tf/envSetting.h"
#include "pxr/base/tf/instantiateSingleton.h"

PXR_NAMESPACE_OPEN_SCOPE
TF_INSTANTIATE_SINGLETON(tr::HdTaurayConfig);
TF_DEFINE_ENV_SETTING(
    HDTAURAY_ACCUMULATE,
    tr::HdTaurayDefaultAccumulate,
    "Accumulate"
);
TF_DEFINE_ENV_SETTING(
    HDTAURAY_SAMPLES,
    tr::HdTaurayDefaultSamples,
    "Target samples per pixel"
);
TF_DEFINE_ENV_SETTING(
    HDTAURAY_SAMPLEBATCH,
    tr::HdTaurayDefaultSampleBatch,
    "Samples per batch"
);
TF_DEFINE_ENV_SETTING(
    HDTAURAY_RAYDEPTH,
    tr::HdTaurayDefaultRayDepth,
    "Maximum ray depth"
);
PXR_NAMESPACE_CLOSE_SCOPE

namespace tr
{

HdTaurayConfig::HdTaurayConfig()
{
    accumulate = pxr::TfGetEnvSetting(pxr::HDTAURAY_ACCUMULATE);
    samples = pxr::TfGetEnvSetting(pxr::HDTAURAY_SAMPLES);
    sampleBatch = pxr::TfGetEnvSetting(pxr::HDTAURAY_SAMPLEBATCH);
    rayDepth = pxr::TfGetEnvSetting(pxr::HDTAURAY_RAYDEPTH);
}

const HdTaurayConfig& HdTaurayConfig::GetInstance()
{
    return pxr::TfSingleton<HdTaurayConfig>::GetInstance();
}

}
