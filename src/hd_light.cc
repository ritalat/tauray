#include "hd_light.hh"
#include "hd_render_delegate.hh"

#include "pxr/base/gf/matrix4f.h"
#include "pxr/imaging/hd/light.h"
#include "pxr/imaging/hd/sceneDelegate.h"

#include <mutex>
#include <iostream>

namespace tr
{

HdTaurayLight::HdTaurayLight(pxr::SdfPath const& id, pxr::TfToken const& lightType)
:   pxr::HdLight(id),
    _type(lightType),
    _id(INVALID_ENTITY)
{
}

HdTaurayLight::~HdTaurayLight()
{
}

pxr::HdDirtyBits HdTaurayLight::GetInitialDirtyBitsMask() const
{
    return pxr::HdLight::AllDirty;
}

void HdTaurayLight::Sync(
    pxr::HdSceneDelegate* sceneDelegate,
    pxr::HdRenderParam* renderParam,
    pxr::HdDirtyBits* dirtyBits
)
{
    HdTaurayRenderParam *param = static_cast<HdTaurayRenderParam*>(renderParam);
    std::lock_guard<std::mutex> lock(*(param->mtx));

    TR_DBG("Sync Light id=", GetId().GetText());

    if(_id == INVALID_ENTITY) // FIXME: Sync!!
    {
        _id = param->s->add(
            transformable(),
            name_component{GetId().GetAsString()}
        );
        transformable* tnode = param->s->get<transformable>(_id);

        pxr::HdTimeSampleArray<pxr::GfMatrix4d, 1> transforms;
        sceneDelegate->SampleTransform(GetId(), &transforms);
        pxr::GfMatrix4f transform0(transforms.values[0]);

        tnode->set_transform(glm::make_mat4(transform0.data()));

        vec3 color = glm::make_vec3(
            sceneDelegate->GetLightParamValue(
                GetId(),
                pxr::HdLightTokens->color
            ).GetWithDefault(pxr::GfVec3f{1.0f, 1.0f, 1.0f}).data()
        );

        float intensity = sceneDelegate->GetLightParamValue(
            GetId(),
            pxr::HdLightTokens->intensity
        ).GetWithDefault(1.0f);
        // FIXME: This should probably be done in Tauray proper :)
        //  https://github.com/KhronosGroup/glTF-Blender-IO/issues/564
        //  https://github.com/KhronosGroup/glTF-Blender-IO/pull/1760
        //intensity = intensity / 683.0f;

        if(_type == pxr::HdPrimTypeTokens->sphereLight)
        {
            point_light pl;
            // FIXME: See above :)
            //intensity = intensity * 4.0f * (float)M_PI;
            // HACK: Eyeballed by comparing with Cycles :)
            //  Disregard the above math if using this
            //intensity *= 0.1;
            pl.set_color(color * intensity);
            pl.set_radius(
                sceneDelegate->GetLightParamValue(
                    GetId(),
                    pxr::HdLightTokens->radius
                ).GetWithDefault(0.0f)
            );
            param->s->attach(_id, std::move(pl));
        }
        else if(_type == pxr::HdPrimTypeTokens->distantLight)
        {
            directional_light dl;
            dl.set_color(color /** intensity*/);
            dl.set_angle(
                sceneDelegate->GetLightParamValue(
                    GetId(),
                    pxr::HdLightTokens->angle
                ).GetWithDefault(0.0f)
            );
            param->s->attach(_id, std::move(dl));
        }
    }

    // TODO: USD doesn't have spotlights directly
    //  Instead you modify light with the shaping api (shapingConeAngle?)

    *dirtyBits &= ~pxr::HdLight::AllDirty;
}

void HdTaurayLight::Finalize(pxr::HdRenderParam* renderParam)
{
    // FIXME: Cleanup
}

}
