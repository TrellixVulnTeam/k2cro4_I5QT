// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// Tests for the BufferTracker.

#include "gpu/command_buffer/client/buffer_tracker.h"

#include <GLES2/gl2ext.h>
#include "base/memory/scoped_ptr.h"
#include "gpu/command_buffer/client/client_test_helper.h"
#include "gpu/command_buffer/client/gles2_cmd_helper.h"
#include "gpu/command_buffer/client/mapped_memory.h"
#include "gpu/command_buffer/common/command_buffer.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace gpu {
namespace gles2 {

class BufferTrackerTest : public testing::Test {
 protected:
  static const int32 kNumCommandEntries = 400;
  static const int32 kCommandBufferSizeBytes =
      kNumCommandEntries * sizeof(CommandBufferEntry);

  virtual void SetUp() {
    command_buffer_.reset(new MockClientCommandBuffer());
    helper_.reset(new GLES2CmdHelper(command_buffer_.get()));
    helper_->Initialize(kCommandBufferSizeBytes);
    mapped_memory_.reset(new MappedMemoryManager(helper_.get()));
    buffer_tracker_.reset(new BufferTracker(mapped_memory_.get()));
  }

  virtual void TearDown() {
    buffer_tracker_.reset();
    mapped_memory_.reset();
    helper_.reset();
    command_buffer_.reset();
  }

  scoped_ptr<CommandBuffer> command_buffer_;
  scoped_ptr<GLES2CmdHelper> helper_;
  scoped_ptr<MappedMemoryManager> mapped_memory_;
  scoped_ptr<BufferTracker> buffer_tracker_;
};

TEST_F(BufferTrackerTest, Basic) {
  const GLuint kId1 = 123;
  const GLuint kId2 = 124;
  const GLsizeiptr size = 64;

  // Check we can create a Buffer.
  BufferTracker::Buffer* buffer = buffer_tracker_->CreateBuffer(kId1, size);
  ASSERT_TRUE(buffer != NULL);
  // Check we can get the same Buffer.
  EXPECT_EQ(buffer, buffer_tracker_->GetBuffer(kId1));
  // Check mapped memory address.
  EXPECT_TRUE(buffer->address() != NULL);
  // Check shared memory was allocated.
  EXPECT_EQ(1lu, mapped_memory_->num_chunks());
  // Check we get nothing for a non-existent buffer.
  EXPECT_TRUE(buffer_tracker_->GetBuffer(kId2) == NULL);
  // Check we can delete the buffer.
  buffer_tracker_->RemoveBuffer(kId1);
  // Check shared memory was freed.
  mapped_memory_->FreeUnused();
  EXPECT_EQ(0lu, mapped_memory_->num_chunks());
  // Check we get nothing for a non-existent buffer.
  EXPECT_TRUE(buffer_tracker_->GetBuffer(kId1) == NULL);
}

}  // namespace gles2
}  // namespace gpu
