#include "registry/registry_protocol.em"
#include "dynamic_acl_protocol.em"
#include "common/hashmap.em"

type EntryTick(int value) {
  return value;
}

kernel(VM vm) {
  kernalId("elara.os.entry");

  static {
    set_worker_privilege(dynamic_acl_authority, 100);
  }

  start_worker(entry_idle);
  start_worker(dynamic_acl_authority);

  int wid = 0;
  while (wid = kernel_wait_signal()) {
    wid = wid;
  }
}

acl {
  "elara.os.security" -> dynamic_acl_authority;
  "elara.os.registry" -> dynamic_acl_authority;
  "elara.os.boot" -> dynamic_acl_authority;
  "elara.os.frame_io" -> dynamic_acl_authority;
  "elara.os.console_view" -> dynamic_acl_authority;
  "elara.os.window_manager" -> dynamic_acl_authority;
  "elara.os.filesystem" -> dynamic_acl_authority;
  "elara.os.block_io" -> dynamic_acl_authority;
  "elara.os.stream_io" -> dynamic_acl_authority;
  "elara.os.shell" -> dynamic_acl_authority;
}

worker entry_idle(EntryTick tick) {
  signal();
}

worker dynamic_acl_authority(DynamicACLRequest request) {
  static int initialized;
  static int route_permission_map;
  static int entry_registered;
  static int dynamic_acl_registered;
  static int registry_registered;
  static int frame_registered;
  static int boot_registered;
  static int console_registered;
  static int window_registered;
  static int filesystem_registered;
  static int block_io_registered;
  static int stream_io_registered;
  static int security_registered;
  static int shell_registered;

  local RegistryRequest registry_request;
  int route_allowed = 0;

  if (initialized == 0) {
    route_permission_map = hashmap_u64_init();
    entry_registered = 1;
    dynamic_acl_registered = 1;
    registry_registered = 1;

    registry_request.opcode = registry_request_register_authority();
    registry_request.path_hash_lo = dynamic_acl_authority_entry();
    registry_request.path_hash_hi = 0;
    registry_request.flags = 1;
    far_signal("elara.os.registry", registry_ingress, registry_request);

    registry_request.opcode = registry_request_register_authority();
    registry_request.path_hash_lo = dynamic_acl_authority_dynamic_acl();
    registry_request.path_hash_hi = dynamic_acl_authority_entry();
    registry_request.flags = 1;
    far_signal("elara.os.registry", registry_ingress, registry_request);

    initialized = 1;
  }

  if (request.opcode == dynamic_acl_opcode_register()) {
    registry_request.opcode = registry_request_register_authority();
    registry_request.path_hash_lo = request.route_id;
    registry_request.path_hash_hi = request.flags;
    registry_request.flags = 1;
    far_signal("elara.os.registry", registry_ingress, registry_request);

    if (request.route_id == dynamic_acl_authority_registry()) {
      registry_registered = 1;
    }
    if (request.route_id == dynamic_acl_authority_frame_io()) {
      frame_registered = 1;
    }
    if (request.route_id == dynamic_acl_authority_boot()) {
      boot_registered = 1;
    }
    if (request.route_id == dynamic_acl_authority_console()) {
      console_registered = 1;
    }
    if (request.route_id == dynamic_acl_authority_window()) {
      window_registered = 1;
    }
    if (request.route_id == dynamic_acl_authority_filesystem()) {
      filesystem_registered = 1;
    }
    if (request.route_id == dynamic_acl_authority_block_io()) {
      block_io_registered = 1;
    }
    if (request.route_id == dynamic_acl_authority_stream_io()) {
      stream_io_registered = 1;
    }
    if (request.route_id == dynamic_acl_authority_security()) {
      security_registered = 1;
    }
    if (request.route_id == dynamic_acl_authority_shell()) {
      shell_registered = 1;
    }
  }

  if (request.opcode == dynamic_acl_opcode_grant()) {
    hashmap_u32_put(route_permission_map, request.route_id, hashmap_value_new_inline_i32(1, dynamic_acl_permission_asked(), 0));

    route_allowed = 0;
    if (request.route_id == dynamic_acl_route_frame_io_boot()) {
      if (frame_registered == 1) {
        if (boot_registered == 1) {
          route_allowed = 1;
        }
      }
    }
    if (request.route_id == dynamic_acl_route_frame_io_console()) {
      if (frame_registered == 1) {
        if (console_registered == 1) {
          route_allowed = 1;
        }
      }
    }
    if (request.route_id == dynamic_acl_route_frame_io_window()) {
      if (frame_registered == 1) {
        if (window_registered == 1) {
          route_allowed = 1;
        }
      }
    }
    if (request.route_id == dynamic_acl_route_registry_fs()) {
      if (registry_registered == 1) {
        if (filesystem_registered == 1) {
          route_allowed = 1;
        }
      }
    }
    if (request.route_id == dynamic_acl_route_registry_blockio()) {
      if (registry_registered == 1) {
        if (block_io_registered == 1) {
          route_allowed = 1;
        }
      }
    }

    if (route_allowed == 1) {
      hashmap_u32_put(route_permission_map, request.route_id, hashmap_value_new_inline_i32(1, dynamic_acl_permission_granted(), 0));
    }

    if (request.route_id == dynamic_acl_route_frame_io_boot()) {
      if (route_allowed == 1) {
      grant_kernel_route("elara.os.frame_io", "elara.os.boot", publish_boot_frame);
      }
    }

    if (request.route_id == dynamic_acl_route_frame_io_console()) {
      if (route_allowed == 1) {
      grant_kernel_route("elara.os.frame_io", "elara.os.console_view", manager_ingress);
      }
    }

    if (request.route_id == dynamic_acl_route_frame_io_window()) {
      if (route_allowed == 1) {
      grant_kernel_route("elara.os.frame_io", "elara.os.window_manager", manager_ingress);
      }
    }

    if (request.route_id == dynamic_acl_route_registry_fs()) {
      if (route_allowed == 1) {
      grant_kernel_route("elara.os.registry", "elara.os.filesystem", registry_ingress);
      }
    }

    if (request.route_id == dynamic_acl_route_registry_blockio()) {
      if (route_allowed == 1) {
      grant_kernel_route("elara.os.registry", "elara.os.block_io", registry_ingress);
      }
    }
  }

  if (request.opcode == dynamic_acl_opcode_revoke()) {
    if (request.route_id == dynamic_acl_route_frame_io_boot()) {
      revoke_kernel_route("elara.os.frame_io", "elara.os.boot", publish_boot_frame);
    }

    if (request.route_id == dynamic_acl_route_frame_io_console()) {
      revoke_kernel_route("elara.os.frame_io", "elara.os.console_view", manager_ingress);
    }

    if (request.route_id == dynamic_acl_route_frame_io_window()) {
      revoke_kernel_route("elara.os.frame_io", "elara.os.window_manager", manager_ingress);
    }

    if (request.route_id == dynamic_acl_route_registry_fs()) {
      revoke_kernel_route("elara.os.registry", "elara.os.filesystem", registry_ingress);
    }

    if (request.route_id == dynamic_acl_route_registry_blockio()) {
      revoke_kernel_route("elara.os.registry", "elara.os.block_io", registry_ingress);
    }
    hashmap_u32_put(route_permission_map, request.route_id, hashmap_value_new_inline_i32(1, dynamic_acl_permission_asked(), 0));
  }

  if (request.opcode == dynamic_acl_opcode_revoke_all()) {
    if (request.route_id == dynamic_acl_route_frame_io_boot()) {
      revoke_kernel_routes("elara.os.frame_io", "elara.os.boot");
    }

    if (request.route_id == dynamic_acl_route_frame_io_console()) {
      revoke_kernel_routes("elara.os.frame_io", "elara.os.console_view");
    }

    if (request.route_id == dynamic_acl_route_frame_io_window()) {
      revoke_kernel_routes("elara.os.frame_io", "elara.os.window_manager");
    }

    if (request.route_id == dynamic_acl_route_registry_fs()) {
      revoke_kernel_routes("elara.os.registry", "elara.os.filesystem");
    }

    if (request.route_id == dynamic_acl_route_registry_blockio()) {
      revoke_kernel_routes("elara.os.registry", "elara.os.block_io");
    }
    hashmap_u32_put(route_permission_map, request.route_id, hashmap_value_new_inline_i32(1, dynamic_acl_permission_asked(), 0));
  }

  host_signal();
}
