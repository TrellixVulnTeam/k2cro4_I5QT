// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef NET_URL_REQUEST_URL_REQUEST_FILE_JOB_H_
#define NET_URL_REQUEST_URL_REQUEST_FILE_JOB_H_

#include <string>
#include <vector>

#include "base/file_path.h"
#include "net/base/net_export.h"
#include "net/http/http_byte_range.h"
#include "net/url_request/url_request.h"
#include "net/url_request/url_request_job.h"

namespace base{
struct PlatformFileInfo;
}
namespace file_util {
struct FileInfo;
}

namespace net {

class FileStream;

// A request job that handles reading file URLs
class NET_EXPORT URLRequestFileJob : public URLRequestJob {
 public:
  URLRequestFileJob(URLRequest* request,
                    NetworkDelegate* network_delegate,
                    const FilePath& file_path);

  static URLRequest::ProtocolFactory Factory;

  // URLRequestJob:
  virtual void Start() OVERRIDE;
  virtual void Kill() OVERRIDE;
  virtual bool ReadRawData(IOBuffer* buf,
                           int buf_size,
                           int* bytes_read) OVERRIDE;
  virtual bool IsRedirectResponse(GURL* location,
                                  int* http_status_code) OVERRIDE;
  virtual Filter* SetupFilter() const OVERRIDE;
  virtual bool GetMimeType(std::string* mime_type) const OVERRIDE;
  virtual void SetExtraRequestHeaders(
      const HttpRequestHeaders& headers) OVERRIDE;

 protected:
  virtual ~URLRequestFileJob();

  // The OS-specific full path name of the file
  FilePath file_path_;

 private:
  // Callback after fetching file info on a background thread.
  void DidResolve(bool exists, const base::PlatformFileInfo& file_info);

  // Callback after data is asynchronously read from the file.
  void DidRead(int result);

  scoped_ptr<FileStream> stream_;
  bool is_directory_;

  HttpByteRange byte_range_;
  int64 remaining_bytes_;

  // The initial file metadata is fetched on a background thread.
  // AsyncResolver runs that task.
  class AsyncResolver;
  friend class AsyncResolver;
  scoped_refptr<AsyncResolver> async_resolver_;

  DISALLOW_COPY_AND_ASSIGN(URLRequestFileJob);
};

}  // namespace net

#endif  // NET_URL_REQUEST_URL_REQUEST_FILE_JOB_H_
