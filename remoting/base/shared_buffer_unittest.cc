// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "remoting/base/shared_buffer.h"
#include "testing/gtest/include/gtest/gtest.h"

const uint32 kBufferSize = 4096;
const int kPattern = 0x12345678;

const intptr_t kIdZero = 0;
const intptr_t kIdOne = 1;

namespace remoting {

TEST(SharedBufferTest, Basic) {
  scoped_refptr<SharedBuffer> source(new SharedBuffer(kBufferSize));

  // Make sure that the buffer is allocated, the size is recorded correctly and
  // its ID is reset.
  EXPECT_TRUE(source->ptr() != NULL);
  EXPECT_EQ(source->id(), kIdZero);
  EXPECT_EQ(source->size(), kBufferSize);

  // See if setting of the ID works.
  source->set_id(kIdOne);
  EXPECT_EQ(source->id(), kIdOne);

  scoped_refptr<SharedBuffer> dest(
      new SharedBuffer(kIdZero, source->handle(), kBufferSize));

  // Make sure that the buffer is allocated, the size is recorded correctly and
  // its ID is reset.
  EXPECT_TRUE(dest->ptr() != NULL);
  EXPECT_EQ(dest->id(), kIdZero);
  EXPECT_EQ(dest->size(), kBufferSize);

  // Verify that the memory contents are the same for the two buffers.
  int* source_ptr = reinterpret_cast<int*>(source->ptr());
  *source_ptr = kPattern;
  int* dest_ptr = reinterpret_cast<int*>(dest->ptr());
  EXPECT_EQ(*source_ptr, *dest_ptr);

  // Check that the destination buffer is still mapped even when the source
  // buffer is destroyed.
  source = NULL;
  EXPECT_EQ(0x12345678, *dest_ptr);
}

}  // namespace remoting
