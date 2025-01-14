# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

{
  'includes': [
    '../../../../../native_client/build/common.gypi',
  ],
  'targets': [
    {
      'target_name': 'ppruntime_lib',
      'type': 'none',
      'dependencies': [
        '<(DEPTH)/native_client/tools.gyp:prep_toolchain',
        '<(DEPTH)/media/media_untrusted.gyp:shared_memory_support_untrusted',
        '<(DEPTH)/third_party/khronos/khronos.gyp:headers',
      ],
      'variables': {
        'nlib_target': 'libppruntime.a',
        'build_glibc': 0,
        'build_newlib': 1,
        'include_dirs': [
          '<(DEPTH)/gpu',
          '<(DEPTH)/media',
          '<(DEPTH)/ppapi/native_client/src/shared/ppapi_proxy/untrusted',
        ],
        'sources': [
          '<(DEPTH)/gpu/command_buffer/common/cmd_buffer_common.cc',
          '<(DEPTH)/gpu/command_buffer/common/debug_marker_manager.cc',
          '<(DEPTH)/gpu/command_buffer/common/gles2_cmd_format.cc',
          '<(DEPTH)/gpu/command_buffer/common/gles2_cmd_utils.cc',
          '<(DEPTH)/gpu/command_buffer/common/logging.cc',

          '<(DEPTH)/gpu/command_buffer/client/atomicops.cc',
          '<(DEPTH)/gpu/command_buffer/client/buffer_tracker.cc',
          '<(DEPTH)/gpu/command_buffer/client/cmd_buffer_helper.cc',
          '<(DEPTH)/gpu/command_buffer/client/fenced_allocator.cc',
          '<(DEPTH)/gpu/command_buffer/client/gles2_c_lib.cc',
          '<(DEPTH)/gpu/command_buffer/client/gles2_cmd_helper.cc',
          '<(DEPTH)/gpu/command_buffer/client/gles2_implementation.cc',
          '<(DEPTH)/gpu/command_buffer/client/gles2_interface.cc',
          '<(DEPTH)/gpu/command_buffer/client/program_info_manager.cc',
          '<(DEPTH)/gpu/command_buffer/client/transfer_buffer.cc',
          '<(DEPTH)/gpu/command_buffer/client/gles2_lib.cc',
          '<(DEPTH)/gpu/command_buffer/client/mapped_memory.cc',
          '<(DEPTH)/gpu/command_buffer/client/query_tracker.cc',
          '<(DEPTH)/gpu/command_buffer/client/share_group.cc',
          '<(DEPTH)/gpu/command_buffer/client/ring_buffer.cc',
          '<(DEPTH)/gpu/command_buffer/common/id_allocator.cc',

          'command_buffer_nacl.cc',
          'input_event_data.cc',
          'object_serialize.cc',
          'plugin_callback.cc',
          'plugin_globals.cc',
          'plugin_instance_data.cc',
          'plugin_main.cc',
          'plugin_opengles.cc',
          'plugin_ppb.cc',
          'plugin_ppb_audio.cc',
          'plugin_ppb_audio_config.cc',
          'plugin_ppb_buffer.cc',
          'plugin_ppb_core.cc',
          'plugin_ppb_file_io.cc',
          'plugin_ppb_file_ref.cc',
          'plugin_ppb_file_system.cc',
          'plugin_ppb_find.cc',
          'plugin_ppb_font.cc',
          'plugin_ppb_fullscreen.cc',
          'plugin_ppb_gamepad.cc',
          'plugin_ppb_graphics_2d.cc',
          'plugin_ppb_graphics_3d.cc',
          'plugin_ppb_host_resolver_private.cc',
          'plugin_ppb_image_data.cc',
          'plugin_ppb_input_event.cc',
          'plugin_ppb_instance.cc',
          'plugin_ppb_memory.cc',
          'plugin_ppb_messaging.cc',
          'plugin_ppb_mouse_cursor.cc',
          'plugin_ppb_mouse_lock.cc',
          'plugin_ppb_net_address_private.cc',
          'plugin_ppb_network_list_private.cc',
          'plugin_ppb_network_monitor_private.cc',
          'plugin_ppb_tcp_server_socket_private.cc',
          'plugin_ppb_tcp_socket_private.cc',
          'plugin_ppb_testing.cc',
          'plugin_ppb_udp_socket_private.cc',
          'plugin_ppb_url_loader.cc',
          'plugin_ppb_url_request_info.cc',
          'plugin_ppb_url_response_info.cc',
          'plugin_ppb_var.cc',
          'plugin_ppb_view.cc',
          'plugin_ppb_websocket.cc',
          'plugin_ppb_zoom.cc',
          'plugin_ppp_find_rpc_server.cc',
          'plugin_ppp_input_event_rpc_server.cc',
          'plugin_ppp_instance_rpc_server.cc',
          'plugin_ppp_messaging_rpc_server.cc',
          'plugin_ppp_mouse_lock_rpc_server.cc',
          'plugin_ppp_printing_rpc_server.cc',
          'plugin_ppp_selection_rpc_server.cc',
          'plugin_ppp_zoom_rpc_server.cc',
          'plugin_ppp_rpc_server.cc',
          'plugin_resource.cc',
          'plugin_resource_tracker.cc',
          'plugin_upcall.cc',
          'ppp_instance_combined.cc',
          'proxy_var.cc',
          'proxy_var_cache.cc',
          'utility.cc',
          'view_data.cc',
          # Autogenerated files
          'ppp_rpc_server.cc',
          'ppb_rpc_client.cc',
          'upcall_client.cc'
        ],
      },
    },
  ],
}
