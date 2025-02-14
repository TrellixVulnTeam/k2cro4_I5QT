// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROME_BROWSER_SYNC_GLUE_NEW_NON_FRONTEND_DATA_TYPE_CONTROLLER_MOCK_H_
#define CHROME_BROWSER_SYNC_GLUE_NEW_NON_FRONTEND_DATA_TYPE_CONTROLLER_MOCK_H_

#include "chrome/browser/sync/glue/new_non_frontend_data_type_controller.h"
#include "sync/api/sync_error.h"
#include "testing/gmock/include/gmock/gmock.h"

namespace browser_sync {

class NewNonFrontendDataTypeControllerMock
    : public NewNonFrontendDataTypeController {
 public:
  NewNonFrontendDataTypeControllerMock();

  // DataTypeController mocks.
  MOCK_METHOD1(StartAssociating,
                 void(const StartCallback& start_callback));
  MOCK_METHOD1(LoadModels, void(const ModelLoadCallback& model_load_callback));
  MOCK_METHOD0(OnModelLoaded, void());

  MOCK_METHOD0(Stop, void());
  MOCK_METHOD0(enabled, bool());
  MOCK_CONST_METHOD0(type, syncer::ModelType());
  MOCK_CONST_METHOD0(name, std::string());
  MOCK_CONST_METHOD0(model_safe_group, syncer::ModelSafeGroup());
  MOCK_CONST_METHOD0(state, State());
  MOCK_METHOD2(OnUnrecoverableError, void(const tracked_objects::Location&,
                                          const std::string&));

  // NonFrontendDataTypeController mocks.
  MOCK_METHOD0(StartModels, bool());
  MOCK_METHOD2(PostTaskOnBackendThread,
               bool(const tracked_objects::Location&,
                    const base::Closure&));
  MOCK_METHOD0(StartAssociation, void());
  MOCK_METHOD0(CreateSyncComponents, void());
  MOCK_METHOD3(StartDone,
               void(DataTypeController::StartResult result,
                    const syncer::SyncMergeResult& local_merge_result,
                    const syncer::SyncMergeResult& syncer_merge_result));
  MOCK_METHOD4(StartDoneImpl,
               void(DataTypeController::StartResult result,
                    DataTypeController::State new_state,
                    const syncer::SyncMergeResult& local_merge_result,
                    const syncer::SyncMergeResult& syncer_merge_result));
  MOCK_METHOD0(StopModels, void());
  MOCK_METHOD0(StopAssociationAsync, bool());
  MOCK_METHOD0(StopAssociation, void());
  MOCK_METHOD2(OnUnrecoverableErrorImpl, void(const tracked_objects::Location&,
                                              const std::string&));
  MOCK_METHOD2(RecordUnrecoverableError, void(const tracked_objects::Location&,
                                              const std::string&));
  MOCK_METHOD1(RecordAssociationTime, void(base::TimeDelta time));
  MOCK_METHOD1(RecordStartFailure, void(StartResult result));

  // NewNonFrontendDataTypeController mocks.
  MOCK_CONST_METHOD0(GetWeakPtrToSyncableService,
                     base::WeakPtr<syncer::SyncableService>());

 protected:
  virtual ~NewNonFrontendDataTypeControllerMock();
};

}  // namespace browser_sync

#endif  // CHROME_BROWSER_SYNC_GLUE_NEW_NON_FRONTEND_DATA_TYPE_CONTROLLER_MOCK_H_
