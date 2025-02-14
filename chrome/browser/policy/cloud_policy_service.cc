// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/policy/cloud_policy_service.h"

#include "base/callback.h"
#include "chrome/browser/policy/proto/device_management_backend.pb.h"

namespace em = enterprise_management;

namespace policy {

CloudPolicyService::CloudPolicyService(CloudPolicyClient* client,
                                       CloudPolicyStore* store)
    : client_(client),
      store_(store),
      refresh_state_(REFRESH_NONE),
      initialization_complete_(false) {
  client_->AddObserver(this);
  store_->AddObserver(this);

  // Make sure we initialize |client_| from the policy data that might be
  // already present in |store_|.
  OnStoreLoaded(store_);
}

CloudPolicyService::~CloudPolicyService() {
  client_->RemoveObserver(this);
  store_->RemoveObserver(this);
}

std::string CloudPolicyService::ManagedBy() const {
  const em::PolicyData* policy = store_->policy();
  if (policy) {
    std::string username = policy->username();
    std::size_t pos = username.find('@');
    if (pos != std::string::npos)
      return username.substr(pos + 1);
  }
  return std::string();
}

void CloudPolicyService::RefreshPolicy(const base::Closure& callback) {
  // If the client is not registered, bail out.
  if (!client_->is_registered()) {
    callback.Run();
    return;
  }

  // Else, trigger a refresh.
  refresh_callbacks_.push_back(callback);
  refresh_state_ = REFRESH_POLICY_FETCH;
  client_->FetchPolicy();
}

void CloudPolicyService::OnPolicyFetched(CloudPolicyClient* client) {
  if (client_->status() != DM_STATUS_SUCCESS) {
    RefreshCompleted();
    return;
  }

  const em::PolicyFetchResponse* policy = client_->policy();
  if (policy) {
    if (refresh_state_ != REFRESH_NONE)
      refresh_state_ = REFRESH_POLICY_STORE;
    store_->Store(*policy);
  } else {
    RefreshCompleted();
  }
}

void CloudPolicyService::OnRegistrationStateChanged(CloudPolicyClient* client) {
}

void CloudPolicyService::OnClientError(CloudPolicyClient* client) {
  if (refresh_state_ == REFRESH_POLICY_FETCH)
    RefreshCompleted();
}

void CloudPolicyService::OnStoreLoaded(CloudPolicyStore* store) {
  // Update the client with state from the store.
  const em::PolicyData* policy(store_->policy());

  // Timestamp.
  base::Time policy_timestamp;
  if (policy && policy->has_timestamp()) {
    policy_timestamp =
        base::Time::UnixEpoch() +
        base::TimeDelta::FromMilliseconds(policy->timestamp());
  }
  client_->set_last_policy_timestamp(policy_timestamp);

  // Public key version.
  if (policy && policy->has_public_key_version())
    client_->set_public_key_version(policy->public_key_version());
  else
    client_->clear_public_key_version();

  // Whether to submit the machine ID.
  bool submit_machine_id = false;
  if (policy && policy->has_valid_serial_number_missing())
    submit_machine_id = policy->valid_serial_number_missing();
  client_->set_submit_machine_id(submit_machine_id);

  // Finally, set up registration if necessary.
  if (policy && policy->has_request_token() && policy->has_device_id() &&
      !client_->is_registered()) {
    DVLOG(1) << "Setting up registration with request token: "
             << policy->request_token();
    client_->SetupRegistration(policy->request_token(),
                               policy->device_id());
  }

  if (refresh_state_ == REFRESH_POLICY_STORE)
    RefreshCompleted();

  CheckInitializationCompleted();
}

void CloudPolicyService::OnStoreError(CloudPolicyStore* store) {
  if (refresh_state_ == REFRESH_POLICY_STORE)
    RefreshCompleted();
  CheckInitializationCompleted();
}

void CloudPolicyService::CheckInitializationCompleted() {
  if (!IsInitializationComplete() && store_->is_initialized()) {
    initialization_complete_ = true;
    FOR_EACH_OBSERVER(Observer, observers_, OnInitializationCompleted(this));
  }
}

void CloudPolicyService::RefreshCompleted() {
  // Clear state and |refresh_callbacks_| before actually invoking them, s.t.
  // triggering new policy fetches behaves as expected.
  std::vector<base::Closure> callbacks;
  callbacks.swap(refresh_callbacks_);
  refresh_state_ = REFRESH_NONE;

  for (std::vector<base::Closure>::iterator callback(callbacks.begin());
       callback != callbacks.end();
       ++callback) {
    callback->Run();
  }
}

void CloudPolicyService::AddObserver(Observer* observer) {
  observers_.AddObserver(observer);
}

void CloudPolicyService::RemoveObserver(Observer* observer) {
  observers_.RemoveObserver(observer);
}

}  // namespace policy
