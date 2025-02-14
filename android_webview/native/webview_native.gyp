# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
{
  'variables': {
    'chromium_code': 1,
  },
  'targets': [
    {
      'target_name': 'webview_native',
      'type': 'static_library',
      'dependencies': [
        '../../base/base.gyp:base_static',
        '../../content/content.gyp:web_contents_delegate_android',
        'android_webview_native_jni',
      ],
      'include_dirs': [
        '../..',
        '../../skia/config',
        '<(SHARED_INTERMEDIATE_DIR)/android_webview',
      ],
      'sources': [
        'android_protocol_handler.cc',
        'android_protocol_handler.h',
        'android_webview_jni_registrar.cc',
        'android_webview_jni_registrar.h',
        'aw_browser_dependency_factory.cc',
        'aw_browser_dependency_factory.h',
        'aw_contents.cc',
        'aw_contents.h',
        'aw_contents_io_thread_client_impl.cc',
        'aw_contents_io_thread_client_impl.h',
        'aw_http_auth_handler.cc',
        'aw_http_auth_handler.h',
        'aw_javascript_dialog_creator.cc',
        'aw_javascript_dialog_creator.h',
        'aw_resource.cc',
        'aw_resource.h',
        'aw_web_contents_delegate.cc',
        'aw_web_contents_delegate.h',
        'cookie_manager.cc',
        'cookie_manager.h',
        'input_stream_impl.cc',
        'input_stream_impl.h',
        'intercepted_request_data_impl.cc',
        'intercepted_request_data_impl.h',
        'js_result_handler.cc',
        'js_result_handler.h',
        'net_init_native_callback.cc',
        'state_serializer.cc',
        'state_serializer.h',
      ],
    },
    {
      'target_name': 'android_jar_jni_headers',
      'type': 'none',
      'variables': {
        'jni_gen_dir': 'android_webview',
        'input_java_class': 'java/io/InputStream.class',
        'input_jar_file': '<(android_sdk)/android.jar',
      },
      'includes': [ '../../build/jar_file_jni_generator.gypi' ],
    },
    {
      'target_name': 'android_webview_native_jni',
      'type': 'none',
      'sources': [
          '../java/src/org/chromium/android_webview/AndroidProtocolHandler.java',
          '../java/src/org/chromium/android_webview/AwContents.java',
          '../java/src/org/chromium/android_webview/AwContentsIoThreadClient.java',
          '../java/src/org/chromium/android_webview/AwHttpAuthHandler.java',
          '../java/src/org/chromium/android_webview/AwResource.java',
          '../java/src/org/chromium/android_webview/AwWebContentsDelegate.java',
          '../java/src/org/chromium/android_webview/CookieManager.java',
          '../java/src/org/chromium/android_webview/InterceptedRequestData.java',
          '../java/src/org/chromium/android_webview/JsResultHandler.java',
      ],
      'variables': {
        'jni_gen_dir': 'android_webview',
      },
      'includes': [ '../../build/jni_generator.gypi' ],
      'dependencies': [
        'android_jar_jni_headers',
      ],
    },
  ],
}
