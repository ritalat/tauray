#include "hd_render_pass.hh"
#include "hd_config.hh"
#include "hd_render_buffer.hh"
#include "hd_render_delegate.hh"

#include "dshgi_renderer.hh"
#include "load_balancer.hh"
#include "monkeroecs.hh"
#include "rt_renderer.hh"
#include "scene.hh"
#include "sh_renderer.hh"
#include "tauray.hh"

#include "pxr/imaging/hd/camera.h"
#include "pxr/imaging/hd/renderPassState.h"

#include <iostream>

namespace tr
{

HdTaurayRenderPass::HdTaurayRenderPass(
    pxr::HdRenderIndex* index,
    pxr::HdRprimCollection const& collection,
    headless& ctx,
    scene_data& sd,
    options& opt
)
:   pxr::HdRenderPass(index, collection),
    _ctx(ctx),
    _sd(sd),
    _opt(opt),
    _aovBindings(),
    _currentSamples(0),
    _targetSamples(128)
{
}

HdTaurayRenderPass::~HdTaurayRenderPass()
{
    TR_DBG("Destroying renderPass");
}

bool HdTaurayRenderPass::IsConverged() const
{
    if(_aovBindings.empty())
        return false;

    for(auto& binding : _aovBindings)
    {
        if(binding.renderBuffer && !binding.renderBuffer->IsConverged())
            return false;
    }
    return true;
}

renderer* CreateRenderer(headless& ctx, options& opt, scene& s)
{
    tonemap_stage::options tonemap;
    tonemap.tonemap_operator = opt.tonemap;
    tonemap.exposure = opt.exposure;
    tonemap.gamma = opt.gamma;
    tonemap.alpha_grid_background = false;
    tonemap.post_resolve = opt.tonemap_post_resolve;

    bool use_shadow_terminator_fix = false;
    bool has_tri_lights = false;
    bool has_sh_grids = s.count<sh_grid>() != 0;
    bool has_point_lights = s.count<point_light>() + s.count<spotlight>() > 0;
    bool has_directional_lights = s.count<directional_light>() > 0;
    s.foreach([&](model& mod){
        if(mod.get_shadow_terminator_offset() > 0.0f)
            use_shadow_terminator_fix = true;
        for(const model::vertex_group& vg: mod)
        {
            if(vg.mat.emission_factor != vec3(0))
                has_tri_lights = true;
        }
    });

    scene_stage::options scene_options;
    scene_options.max_instances = get_instance_count(s);
    scene_options.max_samplers = get_sampler_count(s);
    scene_options.max_lights = s.count<point_light>() + s.count<spotlight>();
    scene_options.gather_emissive_triangles = has_tri_lights 
        && opt.sample_emissive_triangles > 0;
    scene_options.pre_transform_vertices = opt.pre_transform_vertices;
    scene_options.group_strategy = opt.as_strategy;

    taa_stage::options taa;
    taa.alpha = 1.0f/opt.taa.sequence_length;
    taa.anti_shimmer = opt.taa.anti_shimmer;
    taa.edge_dilation = opt.taa.edge_dilation;

    rt_camera_stage::options rc_opt;
    s.foreach([&](camera& cam){ rc_opt.projection = cam.get_projection_type(); });
    rc_opt.min_ray_dist = opt.min_ray_dist;
    rc_opt.max_ray_depth = opt.max_ray_depth;
    rc_opt.samples_per_pass = min(opt.samples_per_pass, opt.samples_per_pixel);
    // Round sample count to next multiple of samples_per_pass
    rc_opt.samples_per_pixel =
        ((opt.samples_per_pixel + rc_opt.samples_per_pass - 1)
         / rc_opt.samples_per_pass) * rc_opt.samples_per_pass;
    rc_opt.rng_seed = opt.rng_seed;
    rc_opt.local_sampler = opt.sampler;
    rc_opt.transparent_background = opt.transparent_background;
    rc_opt.active_viewport_count =
        opt.spatial_reprojection.size() == 0 ?
        ctx.get_display_count() :
        opt.spatial_reprojection.size();

    if(opt.progress)
    {
        rc_opt.max_passes_per_command_buffer = max(
            rc_opt.samples_per_pixel / rc_opt.samples_per_pass / 100,
            1
        );
    }

    light_sampling_weights sampling_weights;
    sampling_weights.point_lights = has_point_lights 
        ? opt.sample_point_lights : 0.0f;
    sampling_weights.directional_lights = has_directional_lights
        ? opt.sample_directional_lights : 0.0f;
    sampling_weights.envmap = get_environment_map(s)
        ? opt.sample_envmap : 0.0f;
    sampling_weights.emissive_triangles = has_tri_lights
        ? opt.sample_emissive_triangles : 0.0f;

    sh_renderer::options sh;
    (rt_stage::options&)sh = rc_opt;
    sh.samples_per_probe = opt.samples_per_probe;
    sh.film = opt.film;
    sh.film_radius = opt.film_radius;
    sh.mis_mode = opt.multiple_importance_sampling;
    sh.russian_roulette_delta = opt.russian_roulette;
    sh.temporal_ratio = opt.dshgi_temporal_ratio;
    sh.indirect_clamping = opt.indirect_clamping;
    sh.regularization_gamma = opt.regularization;
    sh.sampling_weights = sampling_weights;

    shadow_map_filter sm_filter;
    sm_filter.pcf_samples = min(opt.pcf, 64);
    sm_filter.omni_pcf_samples = min(opt.pcf, 64);
    sm_filter.pcss_samples = min(opt.pcss, 64);
    sm_filter.pcss_minimum_radius = opt.pcss_minimum_radius;

    auto_assign_shadow_maps(
        s,
        opt.shadow_map_resolution,
        vec3(
            opt.shadow_map_radius,
            opt.shadow_map_radius,
            opt.shadow_map_depth
        ),
        vec2(opt.shadow_map_bias/5.0f, opt.shadow_map_bias),
        opt.shadow_map_cascades,
        opt.shadow_map_resolution,
        0.01f,
        vec2(0.005, opt.shadow_map_bias*2)
    );

    if(auto rtype = std::get_if<feature_stage::feature>(&opt.renderer))
    {
        feature_renderer::options rt_opt;
        (rt_camera_stage::options&)rt_opt = rc_opt;
        rt_opt.default_value = vec4(opt.default_value);
        rt_opt.feat = *rtype;
        rt_opt.post_process.tonemap = tonemap;
        rt_opt.scene_options = scene_options;
        return new feature_renderer(ctx, rt_opt);
    }
    else if(auto rtype = std::get_if<options::basic_pipeline_type>(&opt.renderer))
    {
        switch(*rtype)
        {
        case options::PATH_TRACER:
            {
                path_tracer_renderer::options rt_opt;
                (rt_camera_stage::options&)rt_opt = rc_opt;
                rt_opt.use_shadow_terminator_fix =
                    opt.shadow_terminator_fix && use_shadow_terminator_fix;
                rt_opt.use_white_albedo_on_first_bounce =
                    opt.use_white_albedo_on_first_bounce;
                rt_opt.film = opt.film;
                rt_opt.mis_mode = opt.multiple_importance_sampling;
                rt_opt.film_radius = opt.film_radius;
                rt_opt.russian_roulette_delta = opt.russian_roulette;
                rt_opt.indirect_clamping = opt.indirect_clamping;
                rt_opt.regularization_gamma = opt.regularization;
                rt_opt.sampling_weights = sampling_weights;
                rt_opt.bounce_mode = opt.bounce_mode;
                rt_opt.tri_light_mode = opt.tri_light_mode;
                rt_opt.post_process.tonemap = tonemap;
                rt_opt.depth_of_field = opt.depth_of_field.f_stop != 0;
                if(opt.temporal_reprojection > 0.0f)
                    rt_opt.post_process.temporal_reprojection =
                        temporal_reprojection_stage::options{opt.temporal_reprojection, {}};
                if(opt.spatial_reprojection.size() > 0)
                    rt_opt.post_process.spatial_reprojection =
                        spatial_reprojection_stage::options{};
                if(opt.taa.sequence_length != 0)
                    rt_opt.post_process.taa = taa;
                rt_opt.hide_lights = opt.hide_lights;
                rt_opt.accumulate = opt.accumulation;
                rt_opt.post_process.tonemap.reorder = get_viewport_reorder_mask(
                    opt.spatial_reprojection,
                    ctx.get_display_count()
                );
                if (opt.denoiser == options::denoiser_type::SVGF)
                {
                    svgf_stage::options svgf_opt{};
                    svgf_opt.atrous_diffuse_iters = opt.svgf.atrous_diffuse_iter;
                    svgf_opt.atrous_spec_iters = opt.svgf.atrous_spec_iter;
                    svgf_opt.atrous_kernel_radius = opt.svgf.atrous_kernel_radius;
                    svgf_opt.sigma_l = opt.svgf.sigma_l;
                    svgf_opt.sigma_n = opt.svgf.sigma_n;
                    svgf_opt.sigma_z = opt.svgf.sigma_z;
                    svgf_opt.temporal_alpha_color = opt.svgf.min_alpha_color;
                    svgf_opt.temporal_alpha_moments = opt.svgf.min_alpha_moments;
                    rt_opt.post_process.svgf_denoiser = svgf_opt;
                }
                else if(opt.denoiser == options::denoiser_type::BMFR)
                    rt_opt.post_process.bmfr = bmfr_stage::options{ bmfr_stage::bmfr_settings::DIFFUSE_ONLY };
                rt_opt.scene_options = scene_options;
                rt_opt.distribution.strategy = opt.distribution_strategy;
                if(ctx.get_devices().size() == 1)
                    rt_opt.distribution.strategy = DISTRIBUTION_DUPLICATE;
                return new path_tracer_renderer(ctx, rt_opt);
            }
        };
    }
    return nullptr;
}

void HdTaurayRenderPass::_Execute(
    pxr::HdRenderPassStateSharedPtr const& renderPassState,
    pxr::TfTokenVector const &renderTags
)
{
    TR_DBG("Execute RenderPass");

    bool recreate_renderer = false;

    pxr::HdRenderDelegate* delegate = GetRenderIndex()->GetRenderDelegate();
    int samples_setting = delegate->GetRenderSetting<int>(
        pxr::HdTauraySettingsTokens->samples, _targetSamples
    );
    int batch_setting = delegate->GetRenderSetting<int>(
        pxr::HdTauraySettingsTokens->sampleBatch, _opt.samples_per_pixel
    );
    int raydepth_setting = delegate->GetRenderSetting<int>(
        pxr::HdTauraySettingsTokens->rayDepth, _opt.max_ray_depth
    );

    TR_DBG("Samples: ", samples_setting);
    TR_DBG("Batch size: ", batch_setting);
    TR_DBG("Ray depth: ", raydepth_setting);

    if(_targetSamples != samples_setting)
    {
        _targetSamples = samples_setting;
        recreate_renderer = true;
    }

    if(_opt.samples_per_pixel != batch_setting)
    {
        _opt.samples_per_pixel = batch_setting;
        recreate_renderer = true;
    }

    if(_opt.max_ray_depth != raydepth_setting)
    {
        _opt.max_ray_depth = raydepth_setting;
        recreate_renderer = true;
    }

    pxr::HdRenderPassAovBindingVector aovBindings =
        renderPassState->GetAovBindings();

    if(_aovBindings != aovBindings)
        _aovBindings = aovBindings;

    for(auto& binding : _aovBindings)
    {
        if(
            binding.aovName == pxr::HdAovTokens->color &&
            static_cast<HdTaurayRenderBuffer*>(binding.renderBuffer)->Dirty()
        ){
            recreate_renderer = true;
            static_cast<HdTaurayRenderBuffer*>(binding.renderBuffer)->Recreate();
        }
    }

    scene& s = *_sd.s;
    load_balancer lb(_ctx, _opt.workload);

    entity cam_id = INVALID_ENTITY;
    s.foreach([&](entity id, camera_metadata& md){
        if(md.enabled) cam_id = id;
    });

    if(cam_id == INVALID_ENTITY)
    {
        cam_id = s.add(camera{}, transformable{}, camera_metadata{true, 0, true});
    }

    bool cameraMoved = false;
    const pxr::HdCamera* indexCam = renderPassState->GetCamera();

    transformable* camt = s.get<transformable>(cam_id);
    mat4 oldTransform = camt->get_transform();
    camt->set_transform(glm::make_mat4(
        indexCam->GetTransform().data()
    ));

    if(oldTransform != camt->get_transform())
        cameraMoved = true;

    camera* cam = s.get<camera>(cam_id);
    mat4 proj = glm::make_mat4(indexCam->ComputeProjectionMatrix().data());
    proj[1][1] *= -1;
    cam->set_projection_matrix_perspective(proj);

    s.foreach([&](camera_metadata& md){
        md.actively_rendered = _opt.spatial_reprojection.count(md.index);
    });
    set_camera_jitter(s, get_camera_jitter_sequence(_opt.taa.sequence_length, _ctx.get_size()));

    if(!recreate_renderer && !cameraMoved && _currentSamples >= _targetSamples)
    {
        _ctx.set_accumulating(false);
        return;
    }
    else
    {
        _ctx.set_accumulating(true);
    }

    if(!_rr || recreate_renderer)
    {
        _rr.reset(CreateRenderer(_ctx, _opt, s));
        _rr->set_scene(&s);
        lb.update(*_rr);
        _ctx.set_displaying(false);
        for(int i = 0; i < _opt.warmup_frames; ++i)
        {
            if(!_opt.skip_render)
            {
                update(s, 0, true);
                _rr->render();
                lb.update(*_rr);
            }
        }
        _ctx.set_displaying(true);
        _currentSamples = 0;
    }

    update(s, 0, true);

    try
    {
        if(cameraMoved) {
            _rr->reset_accumulation();
            _currentSamples = 0;
        }

        _rr->render();
        if(_opt.timing) _ctx.get_timing().print_last_trace(_opt.trace);
        _currentSamples += batch_setting;
    }
    catch(vk::OutOfDateKHRError& e)
    {
        _rr.reset();
        _currentSamples = 0;
    }

    lb.update(*_rr);

    // Ensure everything is finished before going to destructors.
    _ctx.get_timing().wait_all_frames(_opt.timing, _opt.trace);
}

}
