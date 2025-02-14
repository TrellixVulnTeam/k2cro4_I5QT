// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/chromeos/drive/drive_scheduler.h"

#include <math.h>

#include "base/message_loop.h"
#include "base/rand_util.h"
#include "chrome/browser/chromeos/drive/file_system/drive_operations.h"
#include "chrome/browser/chromeos/drive/file_system/remove_operation.h"
#include "chrome/browser/prefs/pref_service.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/pref_names.h"
#include "content/public/browser/browser_thread.h"

using content::BrowserThread;

namespace drive {

namespace {
const int kMaxThrottleCount = 5;
}

DriveScheduler::JobInfo::JobInfo(JobType in_job_type, FilePath in_file_path)
    : job_type(in_job_type),
      job_id(-1),
      completed_bytes(0),
      total_bytes(0),
      file_path(in_file_path),
      state(STATE_NONE) {
}

DriveScheduler::QueueEntry::QueueEntry(JobType in_job_type,
                                       FilePath in_file_path,
                                       FileOperationCallback in_callback)
    : job_info(in_job_type, in_file_path),
      callback(in_callback),
      is_recursive(false) {
}

DriveScheduler::QueueEntry::~QueueEntry() {
}

DriveScheduler::DriveScheduler(Profile* profile,
                               file_system::DriveOperations* drive_operations)
    : job_loop_is_running_(false),
      next_job_id_(0),
      throttle_count_(0),
      disable_throttling_(false),
      drive_operations_(drive_operations),
      profile_(profile),
      weak_ptr_factory_(ALLOW_THIS_IN_INITIALIZER_LIST(this)),
      initialized_(false) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
}

DriveScheduler::~DriveScheduler() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));
  DCHECK(initialized_);
  net::NetworkChangeNotifier::RemoveConnectionTypeObserver(this);
}

void DriveScheduler::Initialize() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // Initialize() may be called more than once for the lifetime when the
  // file system is remounted.
  if (initialized_)
    return;

  net::NetworkChangeNotifier::AddConnectionTypeObserver(this);
  initialized_ = true;
}

void DriveScheduler::Copy(const FilePath& src_file_path,
                          const FilePath& dest_file_path,
                          const FileOperationCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  scoped_ptr<QueueEntry> new_job(
      new QueueEntry(TYPE_COPY, src_file_path, callback));
  new_job->dest_file_path = dest_file_path;

  QueueJob(new_job.Pass());

  StartJobLoop();
}

void DriveScheduler::TransferFileFromRemoteToLocal(
    const FilePath& remote_src_file_path,
    const FilePath& local_dest_file_path,
    const FileOperationCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  scoped_ptr<QueueEntry> new_job(new QueueEntry(TYPE_TRANSFER_REMOTE_TO_LOCAL,
                                                remote_src_file_path,
                                                callback));
  new_job->dest_file_path = local_dest_file_path;

  QueueJob(new_job.Pass());

  StartJobLoop();
}

void DriveScheduler::TransferFileFromLocalToRemote(
    const FilePath& local_src_file_path,
    const FilePath& remote_dest_file_path,
    const FileOperationCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  scoped_ptr<QueueEntry> new_job(new QueueEntry(TYPE_TRANSFER_LOCAL_TO_REMOTE,
                                                local_src_file_path,
                                                callback));
  new_job->dest_file_path = remote_dest_file_path;

  QueueJob(new_job.Pass());

  StartJobLoop();
}

void DriveScheduler::TransferRegularFile(
    const FilePath& local_src_file_path,
    const FilePath& remote_dest_file_path,
    const FileOperationCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  scoped_ptr<QueueEntry> new_job(new QueueEntry(TYPE_TRANSFER_REGULAR_FILE,
                                                local_src_file_path,
                                                callback));
  new_job->dest_file_path = remote_dest_file_path;

  QueueJob(new_job.Pass());

  StartJobLoop();
}

void DriveScheduler::Move(const FilePath& src_file_path,
                          const FilePath& dest_file_path,
                          const FileOperationCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  scoped_ptr<QueueEntry> new_job(
      new QueueEntry(TYPE_MOVE, src_file_path, callback));
  new_job->dest_file_path = dest_file_path;

  QueueJob(new_job.Pass());

  StartJobLoop();
}

void DriveScheduler::Remove(const FilePath& file_path,
                            bool is_recursive,
                            const FileOperationCallback& callback) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  scoped_ptr<QueueEntry> new_job(
      new QueueEntry(TYPE_REMOVE, file_path, callback));
  new_job->is_recursive = is_recursive;

  QueueJob(new_job.Pass());

  StartJobLoop();
}

int DriveScheduler::QueueJob(scoped_ptr<QueueEntry> job) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  int job_id = next_job_id_;
  job->job_info.job_id = job_id;
  next_job_id_++;

  queue_.push_back(job_id);

  DCHECK(job_info_map_.find(job_id) == job_info_map_.end());
  job_info_map_[job_id] = make_linked_ptr(job.release());

  return job_id;
}

void DriveScheduler::StartJobLoop() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (!job_loop_is_running_)
    DoJobLoop();
}

void DriveScheduler::DoJobLoop() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (queue_.empty() || ShouldStopJobLoop()) {
    // Note that |queue_| is not cleared so the sync loop can resume.
    job_loop_is_running_ = false;
    return;
  }
  job_loop_is_running_ = true;

  // Should copy before calling queue_.pop_front().
  int job_id = queue_.front();
  queue_.pop_front();

  JobMap::iterator job_iter = job_info_map_.find(job_id);
  DCHECK(job_iter != job_info_map_.end());

  JobInfo& job_info = job_iter->second->job_info;
  job_info.state = STATE_RUNNING;

  switch (job_info.job_type) {
    case TYPE_COPY: {
      drive_operations_->Copy(
          job_info.file_path,
          job_iter->second->dest_file_path,
          base::Bind(&DriveScheduler::OnJobDone,
                     weak_ptr_factory_.GetWeakPtr(),
                     job_id));
    }
    break;

    case TYPE_MOVE: {
      drive_operations_->Move(
          job_info.file_path,
          job_iter->second->dest_file_path,
          base::Bind(&DriveScheduler::OnJobDone,
                     weak_ptr_factory_.GetWeakPtr(),
                     job_id));
    }
    break;

    case TYPE_REMOVE: {
      drive_operations_->Remove(
          job_info.file_path,
          job_iter->second->is_recursive,
          base::Bind(&DriveScheduler::OnJobDone,
                     weak_ptr_factory_.GetWeakPtr(),
                     job_id));
    }
    break;

    case TYPE_TRANSFER_LOCAL_TO_REMOTE: {
      drive_operations_->TransferFileFromLocalToRemote(
          job_info.file_path,
          job_iter->second->dest_file_path,
          base::Bind(&DriveScheduler::OnJobDone,
                     weak_ptr_factory_.GetWeakPtr(),
                     job_id));
    }
    break;

    case TYPE_TRANSFER_REGULAR_FILE: {
      drive_operations_->TransferRegularFile(
          job_info.file_path,
          job_iter->second->dest_file_path,
          base::Bind(&DriveScheduler::OnJobDone,
                     weak_ptr_factory_.GetWeakPtr(),
                     job_id));
    }
    break;

    case TYPE_TRANSFER_REMOTE_TO_LOCAL: {
      drive_operations_->TransferFileFromRemoteToLocal(
          job_info.file_path,
          job_iter->second->dest_file_path,
          base::Bind(&DriveScheduler::OnJobDone,
                     weak_ptr_factory_.GetWeakPtr(),
                     job_id));
    }
    break;

    // There is no default case so that there will be a compiler error if a type
    // is added but unhandled.
  }
}

bool DriveScheduler::ShouldStopJobLoop() {
  // Should stop if the gdata feature was disabled while running the fetch
  // loop.
  if (profile_->GetPrefs()->GetBoolean(prefs::kDisableDrive))
    return true;

  // Should stop if the network is not online.
  if (net::NetworkChangeNotifier::IsOffline())
    return true;

  // Should stop if the current connection is on cellular network, and
  // fetching is disabled over cellular.
  if (profile_->GetPrefs()->GetBoolean(prefs::kDisableDriveOverCellular) &&
      net::NetworkChangeNotifier::IsConnectionCellular(
          net::NetworkChangeNotifier::GetConnectionType()))
    return true;

  return false;
}

void DriveScheduler::ThrottleAndContinueJobLoop() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  if (throttle_count_ < kMaxThrottleCount)
    throttle_count_++;

  base::TimeDelta delay;
  if (disable_throttling_) {
    delay = base::TimeDelta::FromSeconds(0);
  } else {
    delay =
      base::TimeDelta::FromSeconds(pow(2, throttle_count_ - 1)) +
      base::TimeDelta::FromMilliseconds(base::RandInt(0, 1000));
  }
  VLOG(1) << "Throttling for " << delay.InMillisecondsF();

  const bool posted = base::MessageLoopProxy::current()->PostDelayedTask(
      FROM_HERE,
      base::Bind(&DriveScheduler::DoJobLoop,
                 weak_ptr_factory_.GetWeakPtr()),
      delay);
  DCHECK(posted);
}

void DriveScheduler::ResetThrottleAndContinueJobLoop() {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  throttle_count_ = 0;
  DoJobLoop();
}

void DriveScheduler::OnJobDone(int job_id, DriveFileError error) {
  JobMap::iterator job_iter = job_info_map_.find(job_id);
  DCHECK(job_iter != job_info_map_.end());

  // Retry, depending on the error.
  if (error == DRIVE_FILE_ERROR_THROTTLED ||
      error == DRIVE_FILE_ERROR_NO_CONNECTION) {
    job_iter->second->job_info.state = STATE_RETRY;

    // Requeue the job.
    queue_.push_back(job_id);
    ThrottleAndContinueJobLoop();
  } else {
    // Handle the callback.
    if (!job_iter->second->callback.is_null()) {
      MessageLoop::current()->PostTask(FROM_HERE,
          base::Bind(job_iter->second->callback, error));
    }

    // Delete the job.
    job_info_map_.erase(job_id);
    ResetThrottleAndContinueJobLoop();
  }
}

void DriveScheduler::OnConnectionTypeChanged(
    net::NetworkChangeNotifier::ConnectionType type) {
  DCHECK(BrowserThread::CurrentlyOn(BrowserThread::UI));

  // Resume the job loop if the network is back online. Note that we don't
  // need to check the type of the network as it will be checked in
  // ShouldStopJobLoop() as soon as the loop is resumed.
  if (!net::NetworkChangeNotifier::IsOffline())
    StartJobLoop();
}

}  // namespace drive
