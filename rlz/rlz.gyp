# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'variables': {
    'chromium_code': 1,
    'variables': {
      # Force rlz to use chrome's networking stack.
      'force_rlz_use_chrome_net%': 1,
    },
    'conditions': [
      ['force_rlz_use_chrome_net or OS!="win"', {
        'rlz_use_chrome_net%': 1,
      }, {
        'rlz_use_chrome_net%': 0,
      }],
    ],
  },
  'target_defaults': {
    'include_dirs': [
      '..',
    ],
  },
  'targets': [
    {
      'target_name': 'rlz_lib',
      'type': 'static_library',
      'dependencies': [
        '../base/base.gyp:base',
        '../base/third_party/dynamic_annotations/dynamic_annotations.gyp:dynamic_annotations',
      ],
      'sources': [
        'chromeos/lib/machine_id_chromeos.cc',
        'chromeos/lib/rlz_value_store_chromeos.cc',
        'chromeos/lib/rlz_value_store_chromeos.h',
        'lib/assert.cc',
        'lib/assert.h',
        'lib/crc32.h',
        'lib/crc32_wrapper.cc',
        'lib/crc8.h',
        'lib/crc8.cc',
        'lib/financial_ping.cc',
        'lib/financial_ping.h',
        'lib/lib_values.cc',
        'lib/machine_id.cc',
        'lib/machine_id.h',
        'lib/recursive_lock.cc',
        'lib/recursive_lock.h',
        'lib/rlz_enums.h',
        'lib/rlz_lib.cc',
        'lib/rlz_lib.h',
        'lib/rlz_lib_clear.cc',
        'lib/lib_values.h',
        'lib/rlz_value_store.h',
        'lib/string_utils.cc',
        'lib/string_utils.h',
        'mac/lib/machine_id_mac.cc',
        'mac/lib/rlz_value_store_mac.mm',
        'mac/lib/rlz_value_store_mac.h',
        'win/lib/lib_mutex.cc',
        'win/lib/lib_mutex.h',
        'win/lib/machine_deal.cc',
        'win/lib/machine_deal.h',
        'win/lib/machine_id_win.cc',
        'win/lib/process_info.cc',
        'win/lib/process_info.h',
        'win/lib/registry_util.cc',
        'win/lib/registry_util.h',
        'win/lib/rlz_lib.h',
        'win/lib/rlz_lib_win.cc',
        'win/lib/rlz_value_store_registry.cc',
        'win/lib/rlz_value_store_registry.h',
        'win/lib/vista_winnt.h',
      ],
      'conditions': [
        ['rlz_use_chrome_net==1', {
          'defines': [
            'RLZ_NETWORK_IMPLEMENTATION_CHROME_NET',
          ],
          'direct_dependent_settings': {
            'defines': [
              'RLZ_NETWORK_IMPLEMENTATION_CHROME_NET',
            ],
          },
          'dependencies': [
            '../build/temp_gyp/googleurl.gyp:googleurl',
            '../net/net.gyp:net',
          ],
        }, {
          'defines': [
            'RLZ_NETWORK_IMPLEMENTATION_WIN_INET',
          ],
          'direct_dependent_settings': {
            'defines': [
              'RLZ_NETWORK_IMPLEMENTATION_WIN_INET',
            ],
          },
        }],
        ['chromeos==0', {
          'sources!': [
            'lib/recursive_lock.cc',
            'lib/recursive_lock.h',
          ]
        }],
      ],
    },
    {
      'target_name': 'rlz_unittests',
      'type': 'executable',
      'dependencies': [
        ':rlz_lib',
        '../base/base.gyp:base',
        '../base/base.gyp:base_prefs',
        '../testing/gmock.gyp:gmock',
        '../testing/gtest.gyp:gtest',
        '../third_party/zlib/zlib.gyp:zlib',
      ],
      'sources': [
        'lib/crc32_unittest.cc',
        'lib/crc8_unittest.cc',
        'lib/financial_ping_test.cc',
        'lib/lib_values_unittest.cc',
        'lib/machine_id_unittest.cc',
        'lib/recursive_lock_unittest.cc',
        'lib/rlz_lib_test.cc',
        'lib/string_utils_unittest.cc',
        'test/rlz_test_helpers.cc',
        'test/rlz_test_helpers.h',
        'test/rlz_unittest_main.cc',
        'win/lib/machine_deal_test.cc',
      ],
      'conditions': [
        ['rlz_use_chrome_net==1', {
          'dependencies': [
            '../net/net.gyp:net_test_support',
          ],
        }],
        ['chromeos==0', {
          'sources!': [
            'lib/recursive_lock_unittest.cc',
          ],
        }]
      ],
    },
    {
      'target_name': 'rlz_id',
      'type': 'executable',
      'dependencies': [
        ':rlz_lib',
      ],
      'sources': [
        'examples/rlz_id.cc',
      ],
    },
  ],
  'conditions': [
    ['OS=="win"', {
      'targets': [
        {
          'target_name': 'rlz',
          'type': 'shared_library',
          'sources': [
            'win/dll/dll_main.cc',
            'win/dll/exports.cc',
          ],
          'dependencies': [
            ':rlz_lib',
            '../third_party/zlib/zlib.gyp:zlib',
          ],
        },
      ],
    }],
  ],
}
