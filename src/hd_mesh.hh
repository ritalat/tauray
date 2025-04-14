#ifndef TAURAY_HD_MESH_HH
#define TAURAY_HD_MESH_HH

#include "pxr/pxr.h"
#include "pxr/imaging/hd/mesh.h"
#include "pxr/imaging/hd/vertexAdjacency.h"
#include "pxr/base/gf/matrix4f.h"

namespace tr
{

class HdTaurayMesh final : public pxr::HdMesh
{
public:
    HdTaurayMesh(pxr::SdfPath const& id);

    ~HdTaurayMesh() override = default;

    pxr::HdDirtyBits GetInitialDirtyBitsMask() const override;

    void Sync(
        pxr::HdSceneDelegate* sceneDelegate,
        pxr::HdRenderParam*   renderParam,
        pxr::HdDirtyBits*     dirtyBits,
        pxr::TfToken const    &reprToken
    ) override;

    virtual void Finalize(pxr::HdRenderParam* renderParam) override;

protected:
    void _InitRepr(
        pxr::TfToken const& reprToken,
        pxr::HdDirtyBits* dirtyBits
    ) override;

    pxr::HdDirtyBits _PropagateDirtyBits(pxr::HdDirtyBits bits) const override;

    void _UpdatePrimvarSources(
        pxr::HdSceneDelegate* sceneDelegate,
        pxr::HdDirtyBits dirtyBits
    );

    pxr::TfTokenVector _UpdateComputedPrimvarSources(
        pxr::HdSceneDelegate* sceneDelegate,
        pxr::HdDirtyBits dirtyBits
    );

    void _UpdateCachedPrimvars(bool newMesh, pxr::HdDirtyBits dirtyBits);

    void _FlattenBuffers();

    void _UpdateScene(bool newMesh,
        pxr::HdDirtyBits dirtyBits,
        pxr::HdRenderParam* renderParam,
        pxr::HdSceneDelegate* sceneDelegate
    );

    void _DumpGeometry(bool full = true);

    HdTaurayMesh(const HdTaurayMesh&) = delete;
    HdTaurayMesh &operator =(const HdTaurayMesh&) = delete;

private:
    pxr::HdMeshTopology _topology;
    pxr::GfMatrix4f _transform;
    pxr::VtVec3fArray _points;

    pxr::VtVec3iArray _triangulatedIndices;
    pxr::VtIntArray _trianglePrimitiveParams;
    pxr::VtVec3fArray _computedNormals;
    bool _authoredNormals;

    pxr::VtVec3fArray _triangulatedNormals;
    pxr::VtVec2fArray _triangulatedTexCoords;
    bool _mustFlatten;

    pxr::Hd_VertexAdjacency _adjency;
    bool _adjacencyValid;
    bool _normalsValid;

    bool _refined;
    bool _smoothNormals;
    bool _doubleSided;
    pxr::HdCullStyle _cullStyle;

    struct PrimvarSource {
        pxr::VtValue data;
        pxr::HdInterpolation interpolation;
    };
    pxr::TfHashMap<pxr::TfToken, PrimvarSource, pxr::TfToken::HashFunctor> _primvarSourceMap;
};

}

#endif
