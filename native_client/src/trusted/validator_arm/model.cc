/*
 * Copyright (c) 2012 The Native Client Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "native_client/src/trusted/validator_arm/model.h"

namespace nacl_arm_dec {

const char* Instruction::ToString(Instruction::Condition cond) {
  static const char conditions[] =
      "eq\0\0ne\0\0cs\0\0cc\0\0mi\0\0pl\0\0vs\0\0vc\0\0"
      "hi\0\0ls\0\0ge\0\0lt\0\0gt\0\0le\0\0al";
  static const size_t offset_to_null_char = 2;
  size_t offset = static_cast<size_t>(cond) << 2;
  return conditions + ((cond <= AL) ? offset : offset_to_null_char);
}

}  // namespace nacl_arm_dec
