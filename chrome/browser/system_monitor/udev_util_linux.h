// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYSTEM_MONITOR_UDEV_UTIL_LINUX_H_
#define CHROME_BROWSER_SYSTEM_MONITOR_UDEV_UTIL_LINUX_H_

#include <libudev.h>

#include <string>

#include "base/memory/scoped_generic_obj.h"

namespace chrome {

// ScopedGenericObj functor for UdevObjectRelease().
class ScopedReleaseUdevObject {
 public:
  void operator()(struct udev* udev) const {
    udev_unref(udev);
  }
};
typedef ScopedGenericObj<struct udev*,
                         ScopedReleaseUdevObject> ScopedUdevObject;

// ScopedGenericObj functor for UdevDeviceObjectRelease().
class ScopedReleaseUdevDeviceObject {
 public:
  void operator()(struct udev_device* device) const {
    udev_device_unref(device);
  }
};
typedef ScopedGenericObj<struct udev_device*,
                         ScopedReleaseUdevDeviceObject> ScopedUdevDeviceObject;

// Wrapper function for udev_device_get_property_value() that also checks for
// valid but empty values.
std::string GetUdevDevicePropertyValue(struct udev_device* udev_device,
                                       const char* key);

}  // namespace chrome

#endif  // CHROME_BROWSER_SYSTEM_MONITOR_UDEV_UTIL_LINUX_H_
