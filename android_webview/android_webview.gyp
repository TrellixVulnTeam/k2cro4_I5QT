# Copyright (c) 2009 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'variables': {
    'chromium_code': 1,
  },
  'includes': [
    'android_webview_tests.gypi',
  ],
  'targets': [
    {
      'target_name': 'libwebviewchromium',
      'type': 'shared_library',
      'android_unmangled_name': 1,
      'dependencies': [
        'android_webview_common',
      ],
      'ldflags': [
        # fix linking to hidden symbols and re-enable this (crbug.com/157326)
        '-Wl,--no-fatal-warnings'
      ],
      'sources': [
        'lib/main/webview_entry_point.cc',
      ],
      'conditions': [
        ['android_build_type != 0', {
          'libraries': [
            # The "android" gyp backend doesn't quite handle static libraries'
            # dependencies correctly; force this to be linked as a workaround.
            'cpufeatures.a',
          ],
        }],
      ],
    },
    {
      'target_name': 'android_webview_common',
      'type': 'static_library',
      'dependencies': [
        '../android_webview/native/webview_native.gyp:webview_native',
        '../content/content.gyp:content',
        '../content/content.gyp:navigation_interception',
        '../content/content.gyp:web_contents_delegate_android',
        '../ui/ui.gyp:ui_resources',
      ],
      'include_dirs': [
        '..',
        '../skia/config',
      ],
      'sources': [
        'browser/aw_browser_context.cc',
        'browser/aw_browser_context.h',
        'browser/aw_browser_main_parts.cc',
        'browser/aw_browser_main_parts.h',
        'browser/aw_content_browser_client.cc',
        'browser/aw_content_browser_client.h',
        'browser/aw_contents_io_thread_client.h',
        'browser/aw_cookie_access_policy.cc',
        'browser/aw_cookie_access_policy.h',
        'browser/aw_http_auth_handler_base.cc',
        'browser/aw_http_auth_handler_base.h',
        'browser/aw_login_delegate.cc',
        'browser/aw_login_delegate.h',
        'browser/aw_quota_permission_context.cc',
        'browser/aw_quota_permission_context.h',
        'browser/aw_request_interceptor.cc',
        'browser/aw_request_interceptor.h',
        'browser/aw_result_codes.h',
        'browser/find_helper.cc',
        'browser/find_helper.h',
        'browser/input_stream.h',
        'browser/intercepted_request_data.h',
        'browser/net/android_stream_reader_url_request_job.cc',
        'browser/net/android_stream_reader_url_request_job.h',
        'browser/net/aw_network_delegate.cc',
        'browser/net/aw_network_delegate.h',
        'browser/net/aw_url_request_context_getter.cc',
        'browser/net/aw_url_request_context_getter.h',
        'browser/net/aw_url_request_job_factory.cc',
        'browser/net/aw_url_request_job_factory.h',
        'browser/net/init_native_callback.h',
        'browser/net/input_stream_reader.cc',
        'browser/net/input_stream_reader.h',
        'browser/net_disk_cache_remover.cc',
        'browser/net_disk_cache_remover.h',
        'browser/renderer_host/aw_render_view_host_ext.cc',
        'browser/renderer_host/aw_render_view_host_ext.h',
        'browser/renderer_host/aw_resource_dispatcher_host_delegate.cc',
        'browser/renderer_host/aw_resource_dispatcher_host_delegate.h',
        'browser/scoped_allow_wait_for_legacy_web_view_api.h',
        'browser/scoped_allow_wait_for_legacy_web_view_api.h',
        'common/android_webview_message_generator.cc',
        'common/android_webview_message_generator.h',
        'common/aw_content_client.cc',
        'common/aw_content_client.h',
        'common/aw_hit_test_data.cc',
        'common/aw_hit_test_data.h',
        'common/aw_resource.h',
        'common/render_view_messages.cc',
        'common/render_view_messages.h',
        'common/url_constants.cc',
        'common/url_constants.h',
        'lib/aw_browser_dependency_factory_impl.cc',
        'lib/aw_browser_dependency_factory_impl.h',
        'lib/main/aw_main_delegate.cc',
        'lib/main/aw_main_delegate.h',
        'public/browser/draw_gl.h',
        'renderer/aw_content_renderer_client.cc',
        'renderer/aw_content_renderer_client.h',
        'renderer/aw_render_process_observer.cc',
        'renderer/aw_render_process_observer.h',
        'renderer/aw_render_view_ext.cc',
        'renderer/aw_render_view_ext.h',
      ],
    },
    {
      'target_name': 'android_webview_java',
      'type': 'none',
      'dependencies': [
        '../content/content.gyp:content_java',
        '../content/content.gyp:navigation_interception_java',
        '../content/content.gyp:web_contents_delegate_android_java',
        '../ui/ui.gyp:ui_java',
      ],
      'variables': {
        'package_name': 'android_webview_java',
        'java_in_dir': '../android_webview/java',
      },
      'includes': [ '../build/java.gypi' ],
    },
    {
      'target_name': 'android_webview_apk',
      'type': 'none',
      'dependencies': [
        '../base/base.gyp:base_java',
        '../chrome/chrome_resources.gyp:packed_extra_resources',
        '../chrome/chrome_resources.gyp:packed_resources',
        '../content/content.gyp:content_java',
        '../content/content.gyp:navigation_interception_java',
        '../content/content.gyp:web_contents_delegate_android_java',
        '../media/media.gyp:media_java',
        '../net/net.gyp:net_java',
        '../ui/ui.gyp:ui_java',
        'libwebviewchromium',
      ],
      'variables': {
        'package_name': 'android_webview',
        'apk_name': 'AndroidWebView',
        'manifest_package_name': 'org.chromium.android_webview',
        'java_in_dir': '../android_webview/java',
        'native_libs_paths': ['<(SHARED_LIB_DIR)/libwebviewchromium.so'],
        'input_pak_files': [
          '<(SHARED_INTERMEDIATE_DIR)/repack/chrome.pak',
          '<(SHARED_INTERMEDIATE_DIR)/repack/chrome_100_percent.pak',
          '<(SHARED_INTERMEDIATE_DIR)/repack/en-US.pak',
        ],
        'copied_pak_files': [
          '<(PRODUCT_DIR)/android_webview/assets/chrome.pak',
          '<(PRODUCT_DIR)/android_webview/assets/chrome_100_percent.pak',
          '<(PRODUCT_DIR)/android_webview/assets/en-US.pak',
        ],
        'additional_input_paths': [ '<@(copied_pak_files)' ],
      },
      'copies': [
        {
          'destination': '<(PRODUCT_DIR)/android_webview/assets',
          'files': [ '<@(input_pak_files)' ]
        },
      ],
      'includes': [ '../build/java_apk.gypi' ],
    },
  ],
}
