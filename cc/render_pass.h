// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CCRenderPass_h
#define CCRenderPass_h

#include "base/basictypes.h"
#include "cc/cc_export.h"
#include "cc/draw_quad.h"
#include "cc/hash_pair.h"
#include "cc/scoped_ptr_hash_map.h"
#include "cc/scoped_ptr_vector.h"
#include "cc/shared_quad_state.h"
#include "third_party/skia/include/core/SkColor.h"
#include "ui/gfx/rect_f.h"
#include <public/WebFilterOperations.h>
#include <public/WebTransformationMatrix.h>
#include <vector>

class SkImageFilter;

namespace cc {

class LayerImpl;
template<typename LayerType, typename SurfaceType>
class OcclusionTrackerBase;
class RenderSurfaceImpl;

struct AppendQuadsData;

typedef OcclusionTrackerBase<LayerImpl, RenderSurfaceImpl> OcclusionTrackerImpl;

// A list of DrawQuad objects, sorted internally in front-to-back order.
class QuadList : public ScopedPtrVector<DrawQuad> {
public:
    typedef reverse_iterator backToFrontIterator;
    typedef const_reverse_iterator constBackToFrontIterator;

    inline backToFrontIterator backToFrontBegin() { return rbegin(); }
    inline backToFrontIterator backToFrontEnd() { return rend(); }
    inline constBackToFrontIterator backToFrontBegin() const { return rbegin(); }
    inline constBackToFrontIterator backToFrontEnd() const { return rend(); }
};

typedef ScopedPtrVector<SharedQuadState> SharedQuadStateList;

class CC_EXPORT RenderPass {
public:
    ~RenderPass();

    struct Id {
        int layerId;
        int index;

        Id(int layerId, int index)
            : layerId(layerId)
            , index(index)
        {
        }

        bool operator==(const Id& other) const { return layerId == other.layerId && index == other.index; }
        bool operator!=(const Id& other) const { return !(*this == other); }
        bool operator<(const Id& other) const { return layerId < other.layerId || (layerId == other.layerId && index < other.index); }
    };

    static scoped_ptr<RenderPass> create(Id, gfx::Rect outputRect, const WebKit::WebTransformationMatrix& transformToRootTarget);

    // A shallow copy of the render pass, which does not include its quads.
    scoped_ptr<RenderPass> copy(Id newId) const;

    void appendQuadsForLayer(LayerImpl*, OcclusionTrackerImpl*, AppendQuadsData&);
    void appendQuadsForRenderSurfaceLayer(LayerImpl*, const RenderPass* contributingRenderPass, OcclusionTrackerImpl*, AppendQuadsData&);
    void appendQuadsToFillScreen(LayerImpl* rootLayer, SkColor screenBackgroundColor, const OcclusionTrackerImpl&);

    const QuadList& quadList() const { return m_quadList; }

    Id id() const { return m_id; }

    // FIXME: Modify this transform when merging the RenderPass into a parent compositor.
    // Transforms from quad's original content space to the root target's content space.
    const WebKit::WebTransformationMatrix& transformToRootTarget() const { return m_transformToRootTarget; }

    // This denotes the bounds in physical pixels of the output generated by this RenderPass.
    const gfx::Rect& outputRect() const { return m_outputRect; }

    gfx::RectF damageRect() const { return m_damageRect; }
    void setDamageRect(gfx::RectF rect) { m_damageRect = rect; }

    const WebKit::WebFilterOperations& filters() const { return m_filters; }
    void setFilters(const WebKit::WebFilterOperations& filters) { m_filters = filters; }

    const WebKit::WebFilterOperations& backgroundFilters() const { return m_backgroundFilters; }
    void setBackgroundFilters(const WebKit::WebFilterOperations& filters) { m_backgroundFilters = filters; }

    SkImageFilter* filter() const { return m_filter; }
    void setFilter(SkImageFilter* filter);

    bool hasTransparentBackground() const { return m_hasTransparentBackground; }
    void setHasTransparentBackground(bool transparent) { m_hasTransparentBackground = transparent; }

    bool hasOcclusionFromOutsideTargetSurface() const { return m_hasOcclusionFromOutsideTargetSurface; }
    void setHasOcclusionFromOutsideTargetSurface(bool hasOcclusionFromOutsideTargetSurface) { m_hasOcclusionFromOutsideTargetSurface = hasOcclusionFromOutsideTargetSurface; }
protected:
    RenderPass(Id, gfx::Rect outputRect, const WebKit::WebTransformationMatrix& transformToRootTarget);

    Id m_id;
    QuadList m_quadList;
    SharedQuadStateList m_sharedQuadStateList;
    WebKit::WebTransformationMatrix m_transformToRootTarget;
    gfx::Rect m_outputRect;
    gfx::RectF m_damageRect;
    bool m_hasTransparentBackground;
    bool m_hasOcclusionFromOutsideTargetSurface;
    WebKit::WebFilterOperations m_filters;
    WebKit::WebFilterOperations m_backgroundFilters;
    SkImageFilter* m_filter;

    DISALLOW_COPY_AND_ASSIGN(RenderPass);
};

} // namespace cc

namespace BASE_HASH_NAMESPACE {
#if defined(COMPILER_MSVC)
template<>
inline size_t hash_value<cc::RenderPass::Id>(const cc::RenderPass::Id& key) {
    return hash_value<std::pair<int, int> >(std::pair<int, int>(key.layerId, key.index));
}
#elif defined(COMPILER_GCC)
template<>
struct hash<cc::RenderPass::Id> {
    size_t operator()(cc::RenderPass::Id key) const {
        return hash<std::pair<int, int> >()(std::pair<int, int>(key.layerId, key.index));
    }
};
#else
#error define a hash function for your compiler
#endif // COMPILER
}

namespace cc {
typedef std::vector<RenderPass*> RenderPassList;
typedef ScopedPtrHashMap<RenderPass::Id, RenderPass> RenderPassIdHashMap;
} // namespace cc

#endif
