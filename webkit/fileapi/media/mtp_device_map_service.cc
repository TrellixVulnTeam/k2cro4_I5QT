// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "webkit/fileapi/media/mtp_device_map_service.h"

#include <string>
#include <utility>

#include "base/stl_util.h"
#include "webkit/fileapi/isolated_context.h"
#include "webkit/fileapi/media/mtp_device_delegate.h"

namespace fileapi {

// static
MTPDeviceMapService* MTPDeviceMapService::GetInstance() {
  return Singleton<MTPDeviceMapService>::get();
}

void MTPDeviceMapService::AddDelegate(
    const FilePath::StringType& device_location,
    scoped_refptr<MTPDeviceDelegate> delegate) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DCHECK(delegate.get());
  DCHECK(!device_location.empty());

  if (ContainsKey(delegate_map_, device_location))
    return;

  delegate_map_[device_location] = delegate;
}

void MTPDeviceMapService::RemoveDelegate(
    const FilePath::StringType& device_location) {
  DCHECK(thread_checker_.CalledOnValidThread());
  DelegateMap::iterator it = delegate_map_.find(device_location);
  DCHECK(it != delegate_map_.end());
  delegate_map_.erase(it);
}

MTPDeviceDelegate* MTPDeviceMapService::GetMTPDeviceDelegate(
    const std::string& filesystem_id) {
  DCHECK(thread_checker_.CalledOnValidThread());
  FilePath device_path;
  if (!IsolatedContext::GetInstance()->GetRegisteredPath(filesystem_id,
                                                         &device_path)) {
    return NULL;
  }

  FilePath::StringType device_location = device_path.value();
  DCHECK(!device_location.empty());

  DelegateMap::const_iterator it = delegate_map_.find(device_location);
  DCHECK(it != delegate_map_.end());
  return it->second.get();
}

MTPDeviceMapService::MTPDeviceMapService() {
  // This object is constructed on UI Thread but the member functions are
  // accessed on IO thread. Therefore, detach from current thread.
  thread_checker_.DetachFromThread();
}

MTPDeviceMapService::~MTPDeviceMapService() {}

}  // namespace fileapi
