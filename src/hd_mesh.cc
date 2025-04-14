#include "hd_mesh.hh"

#include "hd_material.hh"
#include "hd_render_delegate.hh"

#include "pxr/base/gf/vec2f.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/imaging/hd/extComputationUtils.h"
#include "pxr/imaging/hd/material.h"
#include "pxr/imaging/hd/meshUtil.h"
#include "pxr/imaging/hd/smoothNormals.h"
#include "pxr/imaging/hd/vtBufferSource.h"

#include <iostream>
#include <mutex>

namespace tr
{

HdTaurayMesh::HdTaurayMesh(pxr::SdfPath const& id)
:   pxr::HdMesh(id),
    _adjacencyValid(false),
    _normalsValid(false),
    _authoredNormals(false),
    _mustFlatten(false)
{
}

pxr::HdDirtyBits HdTaurayMesh::GetInitialDirtyBitsMask() const
{
    return pxr::HdChangeTracker::Clean
            | pxr::HdChangeTracker::InitRepr
            | pxr::HdChangeTracker::DirtyPoints
            | pxr::HdChangeTracker::DirtyTopology
            | pxr::HdChangeTracker::DirtyTransform
            | pxr::HdChangeTracker::DirtyVisibility
            | pxr::HdChangeTracker::DirtyCullStyle
            | pxr::HdChangeTracker::DirtyDoubleSided
            | pxr::HdChangeTracker::DirtyDisplayStyle
            | pxr::HdChangeTracker::DirtySubdivTags
            | pxr::HdChangeTracker::DirtyPrimvar
            | pxr::HdChangeTracker::DirtyNormals
            | pxr::HdChangeTracker::DirtyInstancer;
}

// FIXME: Create separate meshes for each GeomSubset to preserve material bindings
//  For now meshes with multiple materials must be split by material in blender
// TODO: Instancing
void HdTaurayMesh::Sync(
    pxr::HdSceneDelegate* sceneDelegate,
    pxr::HdRenderParam* renderParam,
    pxr::HdDirtyBits* dirtyBits,
    pxr::TfToken const& reprToken
)
{
#ifdef PROJECT_DEBUG
    HdTaurayRenderParam *param = static_cast<HdTaurayRenderParam*>(renderParam);
    std::lock_guard<std::mutex> lock(*(param->mtx));
#endif
    TR_DBG("Sync Mesh id=", GetId().GetText());

    pxr::SdfPath const& id = GetId();
    pxr::HdMeshReprDesc& desc = _GetReprDesc(reprToken)[0];
    pxr::TfTokenVector computedPrimvars = _UpdateComputedPrimvarSources(
        sceneDelegate,
        *dirtyBits
    );

    bool pointsIsComputed = std::find(
        computedPrimvars.begin(),
        computedPrimvars.end(),
        pxr::HdTokens->points
    ) != computedPrimvars.end();

    if(
        !pointsIsComputed &&
        pxr::HdChangeTracker::IsPrimvarDirty(
            *dirtyBits,
            id,
            pxr::HdTokens->points
        )
    ){
        pxr::VtValue value = sceneDelegate->Get(id, pxr::HdTokens->points);
        _points = value.Get<pxr::VtVec3fArray>();
        _normalsValid = false;
    }

    if(_points.empty())
        throw std::runtime_error("Invalid points");

    if(pxr::HdChangeTracker::IsTopologyDirty(*dirtyBits, id))
    {
        pxr::PxOsdSubdivTags subdivTags = _topology.GetSubdivTags();
        int refineLevel = _topology.GetRefineLevel();
        _topology = pxr::HdMeshTopology(GetMeshTopology(sceneDelegate), refineLevel);
        _topology.SetSubdivTags(subdivTags);
        _adjacencyValid = false;
    }

    if(
        pxr::HdChangeTracker::IsSubdivTagsDirty(*dirtyBits, id) &&
        _topology.GetRefineLevel() > 0
    ){
        _topology.SetSubdivTags(sceneDelegate->GetSubdivTags(id));
    }

    if(pxr::HdChangeTracker::IsDisplayStyleDirty(*dirtyBits, id))
    {
        pxr::HdDisplayStyle const displayStyle = sceneDelegate->GetDisplayStyle(id);
        _topology = pxr::HdMeshTopology(_topology, displayStyle.refineLevel);
    }

    if(pxr::HdChangeTracker::IsTransformDirty(*dirtyBits, id))
    {
        _transform = pxr::GfMatrix4f(sceneDelegate->GetTransform(id));
    }

    if(pxr::HdChangeTracker::IsVisibilityDirty(*dirtyBits, id))
    {
        _UpdateVisibility(sceneDelegate, dirtyBits);
    }

    if(pxr::HdChangeTracker::IsCullStyleDirty(*dirtyBits, id))
    {
        _cullStyle = GetCullStyle(sceneDelegate);
    }

    if(pxr::HdChangeTracker::IsDoubleSidedDirty(*dirtyBits, id))
    {
        _doubleSided = IsDoubleSided(sceneDelegate);
    }

    if(
        pxr::HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, pxr::HdTokens->normals) ||
        pxr::HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, pxr::HdTokens->widths) ||
        pxr::HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, pxr::HdTokens->primvar)
    ){
        _UpdatePrimvarSources(sceneDelegate, *dirtyBits);
    }

    if(desc.geomStyle == pxr::HdMeshGeomStyleSurf)
        TR_DBG("Subdivision unsupported, triangulating id=", id.GetText());

    _smoothNormals = true; //!desc.flatShadingEnabled;
    _authoredNormals = false;
    if(_primvarSourceMap.count(pxr::HdTokens->normals) > 0)
    {
        _authoredNormals = true;
    }
    _smoothNormals = _smoothNormals && !_authoredNormals;

    bool newMesh = false;
    if(pxr::HdChangeTracker::IsTopologyDirty(*dirtyBits, id))
    {
        newMesh = true;
        pxr::HdMeshUtil meshUtil(&_topology, GetId());
        meshUtil.ComputeTriangleIndices(
            &_triangulatedIndices,
            &_trianglePrimitiveParams
        );
        TR_DBG("Triangulated mesh");
    }

    if(_smoothNormals && !_adjacencyValid)
    {
        _adjency.BuildAdjacencyTable(&_topology);
        _adjacencyValid = true;
        _normalsValid = false;
    }

    if(_smoothNormals && !_normalsValid)
    {
        _computedNormals = pxr::Hd_SmoothNormals::ComputeSmoothNormals(
            &_adjency,
            _points.size(),
            _points.data()
        );
        _normalsValid = true;
        TR_DBG("Computed smooth normals");
    }

    if(!_smoothNormals && !_authoredNormals)
        throw std::runtime_error("invalid normals");

    _UpdateCachedPrimvars(newMesh, *dirtyBits);

    if(
        newMesh ||
        pxr::HdChangeTracker::IsPrimvarDirty(
            *dirtyBits,
            id,
            pxr::HdTokens->points
        )
    ){
        TR_DBG("Dirty geometry");
    }

    if(_mustFlatten)
        _FlattenBuffers();

    _UpdateScene(newMesh, *dirtyBits, renderParam, sceneDelegate);

    *dirtyBits &= ~pxr::HdChangeTracker::AllSceneDirtyBits;
}

pxr::HdDirtyBits HdTaurayMesh::_PropagateDirtyBits(pxr::HdDirtyBits bits) const
{
    return bits;
}

void HdTaurayMesh::_InitRepr(
    pxr::TfToken const& reprToken,
    pxr::HdDirtyBits* dirtyBits
)
{
    _ReprVector::iterator it = std::find_if(
        _reprs.begin(),
        _reprs.end(),
        _ReprComparator(reprToken)
    );

    if(it == _reprs.end())
    {
        _reprs.emplace_back(reprToken, pxr::HdReprSharedPtr());
    }
}

void HdTaurayMesh::_UpdatePrimvarSources(
    pxr::HdSceneDelegate* sceneDelegate,
    pxr::HdDirtyBits dirtyBits
)
{
    pxr::SdfPath const& id = GetId();

    pxr::HdPrimvarDescriptorVector primvars;
    for(size_t i = 0; i < pxr::HdInterpolationCount; ++i)
    {
        pxr::HdInterpolation interp = static_cast<pxr::HdInterpolation>(i);
        primvars = GetPrimvarDescriptors(sceneDelegate, interp);

        for(pxr::HdPrimvarDescriptor const& pv : primvars)
        {
            if(
                pxr::HdChangeTracker::IsPrimvarDirty(
                    dirtyBits,
                    id,
                    pv.name
                ) &&
                pv.name != pxr::HdTokens->points
            ){
                _primvarSourceMap[pv.name] = {
                    GetPrimvar(sceneDelegate, pv.name),
                    interp
                };
            }
        }
    }
}

pxr::TfTokenVector HdTaurayMesh::_UpdateComputedPrimvarSources(
    pxr::HdSceneDelegate* sceneDelegate,
    pxr::HdDirtyBits dirtyBits
)
{
    pxr::SdfPath const& id = GetId();

    pxr::HdExtComputationPrimvarDescriptorVector dirtyCompPrimvars;
    for(size_t i = 0; i < pxr::HdInterpolationCount; ++i)
    {
        pxr::HdExtComputationPrimvarDescriptorVector compPrimvars;
        pxr::HdInterpolation interp = static_cast<pxr::HdInterpolation>(i);
        compPrimvars = sceneDelegate->GetExtComputationPrimvarDescriptors(
            GetId(),
            interp
        );

        for(auto const& pv : compPrimvars)
        {
            if(pxr::HdChangeTracker::IsPrimvarDirty(dirtyBits, id, pv.name))
            {
                dirtyCompPrimvars.emplace_back(pv);
            }
        }
    }

    if(dirtyCompPrimvars.empty())
    {
        return pxr::TfTokenVector();
    }

    pxr::HdExtComputationUtils::ValueStore valueStore
        = pxr::HdExtComputationUtils::GetComputedPrimvarValues(
            dirtyCompPrimvars,
            sceneDelegate
    );

    pxr::TfTokenVector compPrimvarNames;
    for(auto const& compPrimvar : dirtyCompPrimvars)
    {
        auto const it = valueStore.find(compPrimvar.name);
        if(it == valueStore.end())
        {
            continue;
        }

        compPrimvarNames.emplace_back(compPrimvar.name);
        if(compPrimvar.name == pxr::HdTokens->points)
        {
            _points = it->second.Get<pxr::VtVec3fArray>();
            _normalsValid = false;
        }
        else
        {
            _primvarSourceMap[compPrimvar.name] = {
                it->second,
                compPrimvar.interpolation
            };
        }
    }

    return compPrimvarNames;
}

void HdTaurayMesh::_UpdateCachedPrimvars(bool newMesh, pxr::HdDirtyBits dirtyBits)
{
    pxr::SdfPath const& id = GetId();

    for(auto& it : _primvarSourceMap)
    {
        if(
            newMesh ||
            pxr::HdChangeTracker::IsPrimvarDirty(
                dirtyBits,
                id,
                it.first
            )
        ){
            TR_DBG("Dirty primvar: ", it.first.GetText(), ":",
                   pxr::TfEnum::GetName(it.second.interpolation));

            if(it.second.interpolation == pxr::HdInterpolationFaceVarying)
            {
                pxr::HdMeshUtil meshUtil(&_topology, GetId());
                pxr::HdVtBufferSource buffer(it.first, it.second.data);
                pxr::VtValue triangulated;
                bool ret = true;

                if(it.first == pxr::HdTokens->normals)
                {
                    _mustFlatten = true;
                    ret = meshUtil.ComputeTriangulatedFaceVaryingPrimvar(
                        buffer.GetData(),
                        buffer.GetNumElements(),
                        buffer.GetTupleType().type,
                        &triangulated
                    );
                    if (ret)
                        _triangulatedNormals = triangulated.Get<pxr::VtVec3fArray>();
                }
                else if(it.first == pxr::TfToken("st"))
                {
                    _mustFlatten = true;
                    ret = meshUtil.ComputeTriangulatedFaceVaryingPrimvar(
                        buffer.GetData(),
                        buffer.GetNumElements(),
                        buffer.GetTupleType().type,
                        &triangulated
                    );
                    if (ret)
                        _triangulatedTexCoords = triangulated.Get<pxr::VtVec2fArray>();
                }
                if(!ret)
                {
                    TR_DBG("Failed to triangulate primvar data");
                    continue;
                }
            }
            else if(
                it.second.interpolation == pxr::HdInterpolationVertex ||
                it.second.interpolation == pxr::HdInterpolationVarying
            ){
                if(it.first == pxr::HdTokens->normals)
                {
                    _triangulatedNormals = it.second.data.Get<pxr::VtVec3fArray>();
                }
                else if(it.first == pxr::TfToken("st"))
                {
                    _triangulatedTexCoords = it.second.data.Get<pxr::VtVec2fArray>();
                }
            }
            else if(it.second.interpolation == pxr::HdInterpolationUniform)
            {
                if(it.first == pxr::HdTokens->normals || it.first == pxr::TfToken("st"))
                {
                    throw std::runtime_error("FIXME: Uniform primvar interpolation!");
                }
            }
        }
    }

    if(_authoredNormals && _triangulatedNormals.empty())
        throw std::runtime_error("Invalid normals");

    if(_triangulatedTexCoords.empty())
        _triangulatedTexCoords.resize(_triangulatedIndices.size() * 3);
}

void HdTaurayMesh::_FlattenBuffers()
{
    TR_DBG("Flattening indices");

    bool flattenNormals = false;
    bool flattenTexCoords = false;

    for(auto const&[var, data] : _primvarSourceMap)
    {
        if(var == pxr::HdTokens->normals && data.interpolation != pxr::HdInterpolationFaceVarying)
            flattenNormals = true;

        if(var == pxr::TfToken("st") && data.interpolation != pxr::HdInterpolationFaceVarying)
            flattenTexCoords = true;
    }

    pxr::VtVec3fArray flatPoints;
    pxr::VtVec3iArray flatIndices;
    pxr::VtVec3fArray flatNormals;
    pxr::VtVec2fArray flatTexCoords;

    flatPoints.resize(_triangulatedIndices.size() * 3);
    flatIndices.resize(_triangulatedIndices.size());

    if(!_authoredNormals || flattenNormals)
        flatNormals.resize(_triangulatedIndices.size() * 3);

    if(flattenTexCoords)
        flatTexCoords.resize(_triangulatedIndices.size() * 3);

    for(int tri = 0; tri < _triangulatedIndices.size(); ++tri)
    {
        flatPoints[tri * 3 + 0] = _points[_triangulatedIndices[tri][0]];
        flatPoints[tri * 3 + 1] = _points[_triangulatedIndices[tri][1]];
        flatPoints[tri * 3 + 2] = _points[_triangulatedIndices[tri][2]];

        flatIndices[tri] = { tri * 3 + 0, tri * 3 + 1, tri * 3 + 2 };

        if(!_authoredNormals)
        {
            flatNormals[tri * 3 + 0] = _computedNormals[
                _triangulatedIndices[tri][0]
            ];
            flatNormals[tri * 3 + 1] = _computedNormals[
                _triangulatedIndices[tri][1]
            ];
            flatNormals[tri * 3 + 2] = _computedNormals[
                _triangulatedIndices[tri][2]
            ];
        }
        else if(flattenNormals)
        {
            flatNormals[tri * 3 + 0] = _triangulatedNormals[
                _triangulatedIndices[tri][0]
            ];
            flatNormals[tri * 3 + 1] = _triangulatedNormals[
                _triangulatedIndices[tri][1]
            ];
            flatNormals[tri * 3 + 2] = _triangulatedNormals[
                _triangulatedIndices[tri][2]
            ];
        }

        if(flattenTexCoords)
        {
            flatTexCoords[tri * 3 + 0] = _triangulatedTexCoords[
                _triangulatedIndices[tri][0]
            ];
            flatTexCoords[tri * 3 + 1] = _triangulatedTexCoords[
                _triangulatedIndices[tri][1]
            ];
            flatTexCoords[tri * 3 + 2] = _triangulatedTexCoords[
                _triangulatedIndices[tri][2]
            ];
        }
    }

    _points = flatPoints;
    _triangulatedIndices = flatIndices;

    if(!_authoredNormals)
        _computedNormals = flatNormals;

    if(flattenNormals)
        _triangulatedNormals = flatNormals;

    if(flattenTexCoords)
        _triangulatedTexCoords = flatTexCoords;

    _DumpGeometry(false);
}

void HdTaurayMesh::_UpdateScene(
    bool newMesh,
    pxr::HdDirtyBits dirtyBits,
    pxr::HdRenderParam* renderParam,
    pxr::HdSceneDelegate* sceneDelegate
)
{
    TR_DBG("Upload geometry");

    HdTaurayRenderParam *param = static_cast<HdTaurayRenderParam*>(renderParam);
#ifndef PROJECT_DEBUG
    std::lock_guard<std::mutex> lock(*(param->mtx));
#endif

    model m;
    material primitive_material;

    pxr::SdfPath const& matId = sceneDelegate->GetMaterialId(GetId());
    pxr::HdRenderIndex& index = sceneDelegate->GetRenderIndex();
    HdTaurayMaterial* mat = static_cast<HdTaurayMaterial*>(index.GetSprim(
        pxr::HdPrimTypeTokens->material,
        matId
    ));

    if(mat)
    {
        primitive_material = mat->GetMaterial();
    }
    else
    {
        primitive_material.albedo_factor = { 0.5f, 0.5f, 0.5f, 1.0f };
        primitive_material.albedo_tex.first = nullptr;
        primitive_material.metallic_factor = 0.5f;
        primitive_material.roughness_factor = 0.5f;
        primitive_material.metallic_roughness_tex.first = nullptr;
        primitive_material.normal_factor = 1.0f;
        primitive_material.normal_tex.first = nullptr;
        primitive_material.ior = 1.45f;
        primitive_material.emission_factor = { 0.0f, 0.0f, 0.0f };
        primitive_material.emission_tex.first = nullptr;
        primitive_material.name = "fallback";
    }

    primitive_material.double_sided = _doubleSided;

    mesh* prim_mesh = new mesh(param->dev);
    std::vector<mesh::vertex>& mesh_vert = prim_mesh->get_vertices();
    std::vector<uint32_t>& mesh_ind = prim_mesh->get_indices();

    mesh_vert.resize(_points.size());
    for(size_t i = 0; i < _points.size(); ++i)
    {
        mesh_vert[i] = {
            vec3(_points[i][0], _points[i][1], _points[i][2]),
            _authoredNormals ?
                vec3(_triangulatedNormals[i][0],
                    _triangulatedNormals[i][1],
                    _triangulatedNormals[i][2]
                ) :
                vec3(_computedNormals[i][0],
                    _computedNormals[i][1],
                    _computedNormals[i][2]
                ),
            vec2(_triangulatedTexCoords[i][0], _triangulatedTexCoords[i][1]),
            vec4(0.0f)
        };
    }

    if(_mustFlatten)
    {
        mesh_ind.resize(_points.size());
        std::iota(mesh_ind.begin(), mesh_ind.end(), 0);
    }
    else
    {
        mesh_ind.resize(_triangulatedIndices.size() * 3);
        for(size_t i = 0; i < _triangulatedIndices.size(); ++i)
        {
            mesh_ind[i * 3 + 0] = _triangulatedIndices[i][0];
            mesh_ind[i * 3 + 1] = _triangulatedIndices[i][1];
            mesh_ind[i * 3 + 2] = _triangulatedIndices[i][2];
        }
    }

    prim_mesh->calculate_tangents();

    param->md->meshes.emplace_back(prim_mesh);
    m.add_vertex_group(primitive_material, prim_mesh);

    entity id = param->s->add(
        transformable(),
        name_component{GetId().GetAsString()}
    );
    transformable* tnode = param->s->get<transformable>(id);

    tnode->set_transform(glm::make_mat4(_transform.data()));
    tnode->set_static(true);

    param->s->attach(id, std::move(m));

    prim_mesh->refresh_buffers();
}

void HdTaurayMesh::Finalize(pxr::HdRenderParam* renderParam)
{
    // FIXME: Cleanup
}

void HdTaurayMesh::_DumpGeometry(bool full)
{
    if(_points.size() > 0)
    {
    TR_DBG("Points: ", _points.size());
        if(full)
        {
            for(auto& vec : _points)
                TR_DBG("(", vec[0], ",", vec[1] , ",", vec[2], ")");
        }
    }

    if(_triangulatedIndices.size() > 0)
    {
    TR_DBG("Indices: ", _triangulatedIndices.size());
        if(full)
        {
            for(auto& vec : _triangulatedIndices)
                TR_DBG("(", vec[0], ",", vec[1], ",", vec[2], ")");
        }
    }

    if(_computedNormals.size() > 0)
    {
    TR_DBG("Computed normals: ", _computedNormals.size());
        if(full)
        {
            for(auto& vec : _computedNormals)
                TR_DBG("(", vec[0], ",", vec[1], ",", vec[2], ")");
        }
    }

    if(_triangulatedNormals.size() > 0)
    {
        TR_DBG("Triangulated normals: ", _triangulatedNormals.size());
        if(full)
        {
            for(auto& vec : _triangulatedTexCoords)
                TR_DBG("(", vec[0], ",", vec[1], ",", vec[2], ")");
        }
    }

    if(_triangulatedTexCoords.size() > 0)
    {
        TR_DBG("Triangulated texcoords: ", _triangulatedTexCoords.size());
        if(full)
        {
            for(auto& vec : _triangulatedTexCoords)
                TR_DBG("(", vec[0], ",", vec[1], ")");
        }
    }
}

}
