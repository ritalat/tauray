#ifndef TAURAY_HD_RENDER_PASS_HH
#define TAURAY_HD_RENDER_PASS_HH

#include "monkeroecs.hh"
#include "options.hh"
#include "tauray.hh"

#include "pxr/pxr.h"
#include "pxr/imaging/hd/renderPass.h"

#include <memory>
#include <vector>

namespace tr
{

class HdTaurayRenderPass final : public pxr::HdRenderPass
{
public:
    HdTaurayRenderPass(
        pxr::HdRenderIndex* index,
        pxr::HdRprimCollection const& collection,
        headless& ctx,
        scene_data& sd,
        options& opt
    );

    virtual ~HdTaurayRenderPass();

    bool IsConverged() const override;

protected:
    void _Execute(
        pxr::HdRenderPassStateSharedPtr const& renderPassState,
        pxr::TfTokenVector const& renderTags
    ) override;

private:
    std::unique_ptr<renderer> _rr;
    headless& _ctx;
    scene_data& _sd;
    options& _opt;
    pxr::HdRenderPassAovBindingVector _aovBindings;
    int _currentSamples;
    int _targetSamples;
};

}

#endif
