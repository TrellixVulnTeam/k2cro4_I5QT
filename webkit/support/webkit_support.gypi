# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'variables': {
    'chromium_code': 1,
  },
  'targets': [
    {
      'target_name': 'webkit_support',
      'type': 'static_library',
      'variables': { 'enable_wexit_time_destructors': 1, },
      'dependencies': [
        '<(DEPTH)/base/base.gyp:base',
        '<(DEPTH)/base/base.gyp:base_i18n',
        '<(DEPTH)/base/third_party/dynamic_annotations/dynamic_annotations.gyp:dynamic_annotations',
        '<(DEPTH)/media/media.gyp:media',
        '<(DEPTH)/net/net.gyp:net',
        '<(DEPTH)/skia/skia.gyp:skia',
        '<(DEPTH)/testing/gtest.gyp:gtest',
        '<(DEPTH)/third_party/hyphen/hyphen.gyp:hyphen',
        '<(DEPTH)/ui/ui.gyp:ui',
        'glue',
        'user_agent',
        'webkit_base',
        'webkit_gpu',
        'webkit_media',
        'webkit_storage',
        'webkit_support_common',
      ],
      'include_dirs': [
        '<(SHARED_INTERMEDIATE_DIR)/webkit', # for a header generated by grit
      ],
      'defines': [
        # Technically not a unit test but require functions available only to
        # unit tests.
        'UNIT_TEST'
      ],
      'sources': [
        'drt_application_mac.h',
        'drt_application_mac.mm',
        'gc_extension.cc',
        'gc_extension.h',
        'platform_support.h',
        'platform_support_android.cc',
        'platform_support_linux.cc',
        'platform_support_mac.mm',
        'platform_support_win.cc',
        'test_media_stream_client.cc',
        'test_media_stream_client.h',
        'test_stream_texture_factory_android.cc',
        'test_stream_texture_factory_android.h',
        'test_webkit_platform_support.cc',
        'test_webkit_platform_support.h',
        'test_webmessageportchannel.cc',
        'test_webmessageportchannel.h',
        'test_webplugin_page_delegate.cc',
        'test_webplugin_page_delegate.h',
        'webkit_support.cc',
        'webkit_support.h',
        'webkit_support_glue.cc',
        'weburl_loader_mock.cc',
        'weburl_loader_mock.h',
        'weburl_loader_mock_factory.cc',
        'weburl_loader_mock_factory.h',
        'web_audio_device_mock.cc',
        'web_audio_device_mock.h',
        'web_gesture_curve_mock.cc',
        'web_gesture_curve_mock.h',
      ],
      'conditions': [
        ['OS=="mac"', {
          'copies': [{
            'destination': '<(SHARED_INTERMEDIATE_DIR)/webkit',
            'files': [
              '../tools/test_shell/resources/missingImage.png',
              '../tools/test_shell/resources/textAreaResizeCorner.png',
            ],
          }],
        },{ # OS!="mac"
          'copies': [{
            'destination': '<(PRODUCT_DIR)/DumpRenderTree_resources',
            'files': [
              '../tools/test_shell/resources/missingImage.gif',
              '../tools/test_shell/resources/textAreaResizeCorner.png',
            ],
          }],
        }],
      ],
    },

    {
      'target_name': 'webkit_support_common',
      'type': 'static_library',
      'variables': { 'enable_wexit_time_destructors': 1, },
      'dependencies': [
        '<(DEPTH)/base/base.gyp:base',
        '<(DEPTH)/crypto/crypto.gyp:crypto',
        '<(DEPTH)/net/net.gyp:net',
        '<(DEPTH)/skia/skia.gyp:skia',
        '<(DEPTH)/third_party/zlib/zlib.gyp:zlib',
        '<(DEPTH)/ui/ui.gyp:ui',
        'glue',
        'user_agent',
        'webkit_support_gfx',
      ],
      'export_dependent_settings': [
        '<(DEPTH)/base/base.gyp:base',
      ],
      'sources': [
        '<(DEPTH)/webkit/tools/test_shell/mac/DumpRenderTreePasteboard.h',
        '<(DEPTH)/webkit/tools/test_shell/mac/DumpRenderTreePasteboard.m',
        '<(DEPTH)/webkit/tools/test_shell/mock_webclipboard_impl.cc',
        '<(DEPTH)/webkit/tools/test_shell/mock_webclipboard_impl.h',
        '<(DEPTH)/webkit/tools/test_shell/simple_appcache_system.cc',
        '<(DEPTH)/webkit/tools/test_shell/simple_appcache_system.h',
        '<(DEPTH)/webkit/tools/test_shell/simple_clipboard_impl.cc',
        '<(DEPTH)/webkit/tools/test_shell/simple_dom_storage_system.cc',
        '<(DEPTH)/webkit/tools/test_shell/simple_dom_storage_system.h',
        '<(DEPTH)/webkit/tools/test_shell/simple_file_system.cc',
        '<(DEPTH)/webkit/tools/test_shell/simple_file_system.h',
        '<(DEPTH)/webkit/tools/test_shell/simple_file_writer.cc',
        '<(DEPTH)/webkit/tools/test_shell/simple_file_writer.h',
        '<(DEPTH)/webkit/tools/test_shell/simple_resource_loader_bridge.cc',
        '<(DEPTH)/webkit/tools/test_shell/simple_resource_loader_bridge.h',
        '<(DEPTH)/webkit/tools/test_shell/simple_socket_stream_bridge.cc',
        '<(DEPTH)/webkit/tools/test_shell/simple_socket_stream_bridge.h',
        '<(DEPTH)/webkit/tools/test_shell/simple_webcookiejar_impl.cc',
        '<(DEPTH)/webkit/tools/test_shell/simple_webcookiejar_impl.h',
        '<(DEPTH)/webkit/tools/test_shell/test_shell_request_context.cc',
        '<(DEPTH)/webkit/tools/test_shell/test_shell_request_context.h',
        '<(DEPTH)/webkit/tools/test_shell/test_shell_webblobregistry_impl.cc',
        '<(DEPTH)/webkit/tools/test_shell/test_shell_webblobregistry_impl.h',
        '<(DEPTH)/webkit/tools/test_shell/test_shell_webmimeregistry_impl.cc',
        '<(DEPTH)/webkit/tools/test_shell/test_shell_webmimeregistry_impl.h',
        '<(DEPTH)/webkit/fileapi/mock_file_system_options.cc',
        '<(DEPTH)/webkit/fileapi/mock_file_system_options.h',
        'simple_database_system.cc',
        'simple_database_system.h',
      ],
      'conditions': [
        ['inside_chromium_build==0', {
          'dependencies': [
            'setup_third_party.gyp:third_party_headers',
          ],
        }],
      ],
    },

    {
      'target_name': 'webkit_support_gfx',
      'type': 'static_library',
      'variables': { 'enable_wexit_time_destructors': 1, },
      'dependencies': [
        '<(DEPTH)/third_party/libpng/libpng.gyp:libpng',
        '<(DEPTH)/third_party/zlib/zlib.gyp:zlib',
      ],
      'sources': [
        'webkit_support_gfx.h',
        'webkit_support_gfx.cc',
      ],
      'include_dirs': [
        '<(DEPTH)',
      ],
      'conditions': [
          ['OS=="android"', {
              'toolsets': ['target', 'host'],
          }],
      ],
    },
  ],
}
