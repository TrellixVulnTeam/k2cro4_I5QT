// Copyright 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_RENDER_PASS_H_
#define CC_RENDER_PASS_H_

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
  struct Id {
    int layer_id;
    int index;

   Id(int layer_id, int index) : layer_id(layer_id), index(index) {}

    bool operator==(const Id& other) const {
      return layer_id == other.layer_id && index == other.index;
    }
    bool operator!=(const Id& other) const {
      return !(*this == other);
    }
    bool operator<(const Id& other) const {
      return layer_id < other.layer_id ||
          (layer_id == other.layer_id && index < other.index);
    }
  };

  ~RenderPass();

  static scoped_ptr<RenderPass> Create();

  // A shallow copy of the render pass, which does not include its quads.
  scoped_ptr<RenderPass> Copy(Id newId) const;

  void SetNew(Id id,
              gfx::Rect output_rect,
              gfx::RectF damage_rect,
              const WebKit::WebTransformationMatrix& transform_to_root_target);

  void SetAll(Id id,
              gfx::Rect output_rect,
              gfx::RectF damage_rect,
              const WebKit::WebTransformationMatrix& transform_to_root_target,
              bool has_transparent_background,
              bool has_occlusion_from_outside_target_surface,
              const WebKit::WebFilterOperations& filters,
              SkImageFilter* filter,
              const WebKit::WebFilterOperations& background_filters);

  // Uniquely identifies the render pass in the compositor's current frame.
  Id id;

  // These are in the space of the render pass' physical pixels.
  gfx::Rect output_rect;
  gfx::RectF damage_rect;

  // Transforms from the origin of the |output_rect| to the origin of the root
  // render pass' |output_rect|.
  WebKit::WebTransformationMatrix transform_to_root_target;

  // If false, the pixels in the render pass' texture are all opaque.
  bool has_transparent_background;

  // If true, then there may be pixels in the render pass' texture that are not
  // complete, since they are occluded.
  bool has_occlusion_from_outside_target_surface;

  // Deprecated post-processing filters, applied to the pixels in the render
  // pass' texture.
  WebKit::WebFilterOperations filters;
  // Post-processing filter applied to the pixels in the render pass' texture.
  SkImageFilter* filter;

  // Post-processing filters, applied to the pixels showing through the
  // background of the render pass, from behind it.
  WebKit::WebFilterOperations background_filters;

  QuadList quad_list;
  SharedQuadStateList shared_quad_state_list;

 protected:
  RenderPass();

  DISALLOW_COPY_AND_ASSIGN(RenderPass);
};

} // namespace cc

namespace BASE_HASH_NAMESPACE {
#if defined(COMPILER_MSVC)
template<>
inline size_t hash_value<cc::RenderPass::Id>(const cc::RenderPass::Id& key) {
  return hash_value<std::pair<int, int> >(
      std::pair<int, int>(key.layer_id, key.index));
}
#elif defined(COMPILER_GCC)
template<>
struct hash<cc::RenderPass::Id> {
  size_t operator()(cc::RenderPass::Id key) const {
    return hash<std::pair<int, int> >()(
        std::pair<int, int>(key.layer_id, key.index));
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

#endif  // CC_RENDER_PASS_H_
