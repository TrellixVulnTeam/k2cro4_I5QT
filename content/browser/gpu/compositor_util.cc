// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "content/public/browser/compositor_util.h"

#include "base/command_line.h"
#include "base/metrics/field_trial.h"
#include "content/public/browser/gpu_data_manager.h"
#include "content/public/common/content_constants.h"
#include "content/public/common/content_switches.h"

namespace content {

namespace {

bool CanDoAcceleratedCompositing() {
  const GpuDataManager* gpu_data_manager = GpuDataManager::GetInstance();
  GpuFeatureType blacklisted_features =
      gpu_data_manager->GetBlacklistedFeatures();

  // Don't run the field trial if gpu access has been blocked or
  // accelerated compositing is blacklisted.
  if (!gpu_data_manager->GpuAccessAllowed() ||
      blacklisted_features & GPU_FEATURE_TYPE_ACCELERATED_COMPOSITING)
    return false;

  // Check for the software rasterizer (SwiftShader).
  if (gpu_data_manager->ShouldUseSoftwareRendering())
    return false;

  const CommandLine& command_line = *CommandLine::ForCurrentProcess();
  if (command_line.HasSwitch(switches::kDisableAcceleratedCompositing))
    return false;

  return true;
}

}  // namespace

bool IsThreadedCompositingEnabled() {
#if defined(OS_WIN) && defined(USE_AURA)
  // We always want compositing on Aura Windows.
  return true;
#endif

  if (!CanDoAcceleratedCompositing())
    return false;

  const GpuDataManager* gpu_data_manager = GpuDataManager::GetInstance();
  GpuFeatureType blacklisted_features =
      gpu_data_manager->GetBlacklistedFeatures();
  // Disallow threaded compositing when texture sharing is blacklisted since
  // this triggers renderer-side readbacks for the thumbnailer / extensions.
  // http://crbug.com/158747
  if (blacklisted_features & GPU_FEATURE_TYPE_TEXTURE_SHARING)
    return false;

  const CommandLine& command_line = *CommandLine::ForCurrentProcess();

  // Command line switches take precedence over field trials.
  if (command_line.HasSwitch(switches::kDisableForceCompositingMode) ||
      command_line.HasSwitch(switches::kDisableThreadedCompositing))
    return false;

  if (command_line.HasSwitch(switches::kEnableThreadedCompositing))
    return true;

  base::FieldTrial* trial =
      base::FieldTrialList::Find(kGpuCompositingFieldTrialName);
  return trial &&
         trial->group_name() == kGpuCompositingFieldTrialThreadEnabledName;
}

bool IsForceCompositingModeEnabled() {
#if defined(OS_WIN) && defined(USE_AURA)
  // We always want compositing on Aura Windows.
  return true;
#endif

  if (!CanDoAcceleratedCompositing())
    return false;

  const CommandLine& command_line = *CommandLine::ForCurrentProcess();

  // Command line switches take precedence over field trials.
  if (command_line.HasSwitch(switches::kDisableForceCompositingMode))
    return false;

  if (command_line.HasSwitch(switches::kForceCompositingMode))
    return true;

  base::FieldTrial* trial =
      base::FieldTrialList::Find(kGpuCompositingFieldTrialName);

  // Force compositing is enabled in both the force compositing
  // and threaded compositing mode field trials.
  return trial &&
        (trial->group_name() ==
            kGpuCompositingFieldTrialForceCompositingEnabledName ||
         trial->group_name() == kGpuCompositingFieldTrialThreadEnabledName);
}

}  // namespace content
