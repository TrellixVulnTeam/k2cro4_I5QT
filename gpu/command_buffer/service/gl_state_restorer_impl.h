// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// This file contains the GLStateRestorerImpl class.

#ifndef GPU_COMMAND_BUFFER_SERVICE_GL_STATE_RESTORER_IMPL_H_
#define GPU_COMMAND_BUFFER_SERVICE_GL_STATE_RESTORER_IMPL_H_

#include "base/compiler_specific.h"
#include "gpu/gpu_export.h"
#include "ui/gl/gl_state_restorer.h"

namespace gpu {
namespace gles2 {
class GLES2Decoder;
}

// This class implements a GLStateRestorer that forwards to a GLES2Decoder.
class GPU_EXPORT GLStateRestorerImpl : public gfx::GLStateRestorer {
 public:
   explicit GLStateRestorerImpl(gles2::GLES2Decoder* decoder);
   virtual ~GLStateRestorerImpl();

   virtual void RestoreState() OVERRIDE;

 private:
   gles2::GLES2Decoder* decoder_;

   DISALLOW_COPY_AND_ASSIGN(GLStateRestorerImpl);
};

}  // namespace gpu

#endif  // GPU_COMMAND_BUFFER_SERVICE_GL_STATE_RESTORER_IMPL_H_

