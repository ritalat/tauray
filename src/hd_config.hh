#ifndef TAURAY_HD_CONFIG_HH
#define TAURAY_HD_CONFIG_HH

#include "pxr/pxr.h"
#include "pxr/base/tf/singleton.h"

namespace tr
{

constexpr bool HdTaurayDefaultAccumulate = true;
constexpr int HdTaurayDefaultSamples = 128;
constexpr int HdTaurayDefaultSampleBatch = 1;
constexpr int HdTaurayDefaultRayDepth = 8;

class HdTaurayConfig {
public:
    static const HdTaurayConfig& GetInstance();

    bool accumulate = HdTaurayDefaultAccumulate;
    int samples = HdTaurayDefaultSamples;
    int sampleBatch = HdTaurayDefaultSampleBatch;
    int rayDepth = HdTaurayDefaultRayDepth;

private:
    HdTaurayConfig();
    ~HdTaurayConfig() = default;

    HdTaurayConfig(const HdTaurayConfig&) = delete;
    HdTaurayConfig& operator=(const HdTaurayConfig&) = delete;

    friend class pxr::TfSingleton<HdTaurayConfig>;
};

}

#endif
