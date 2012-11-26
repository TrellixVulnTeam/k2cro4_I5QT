// Copyright 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_TRANSFERABLE_RESOURCE_H_
#define CC_TRANSFERABLE_RESOURCE_H_

#include <vector>

#include "cc/cc_export.h"
#include "ui/gfx/size.h"
#include "third_party/khronos/GLES2/gl2.h"
#include "third_party/WebKit/Source/Platform/chromium/public/WebCompositorTransferableResourceList.h"

namespace cc {

struct Mailbox {
  Mailbox();
  bool isZero() const;
  void setName(const GLbyte* name);
  GLbyte name[64];
};

struct CC_EXPORT TransferableResource {
  TransferableResource();
  ~TransferableResource();

  unsigned id;
  GLenum format;
  gfx::Size size;
  Mailbox mailbox;
};

typedef std::vector<TransferableResource> TransferableResourceArray;

struct CC_EXPORT TransferableResourceList :
    public NON_EXPORTED_BASE(WebKit::WebCompositorTransferableResourceList) {
  TransferableResourceList();
  ~TransferableResourceList();

  TransferableResourceArray resources;
  unsigned sync_point;
};

}  // namespace cc

#endif  // CC_TRANSFERABLE_RESOURCE_H_
