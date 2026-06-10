type EntryTick(int value) {
  return value;
}

type DynamicACLRequest(int opcode, int route_id, int flags, int reserved) {
  return opcode;
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
}

worker entry_idle(EntryTick tick) {
  signal();
}

worker dynamic_acl_authority(DynamicACLRequest request) {
  if (request.opcode == 1) {
    if (request.route_id == 1) {
      grant_kernel_route("elara.os.frame_authority", "elara.os.boot", publish_boot_frame);
    }

    if (request.route_id == 2) {
      grant_kernel_route("elara.os.frame_authority", "elara.os.console_view", manager_ingress);
    }

    if (request.route_id == 3) {
      grant_kernel_route("elara.os.frame_authority", "elara.os.window_manager", manager_ingress);
    }

    if (request.route_id == 4) {
      grant_kernel_route("elara.os.registry", "elara.os.filesystem", registry_ingress);
    }

    if (request.route_id == 5) {
      grant_kernel_route("elara.os.registry", "elara.os.block_io", registry_ingress);
    }
  }

  if (request.opcode == 2) {
    if (request.route_id == 1) {
      revoke_kernel_route("elara.os.frame_authority", "elara.os.boot", publish_boot_frame);
    }

    if (request.route_id == 2) {
      revoke_kernel_route("elara.os.frame_authority", "elara.os.console_view", manager_ingress);
    }

    if (request.route_id == 3) {
      revoke_kernel_route("elara.os.frame_authority", "elara.os.window_manager", manager_ingress);
    }

    if (request.route_id == 4) {
      revoke_kernel_route("elara.os.registry", "elara.os.filesystem", registry_ingress);
    }

    if (request.route_id == 5) {
      revoke_kernel_route("elara.os.registry", "elara.os.block_io", registry_ingress);
    }
  }

  if (request.opcode == 3) {
    if (request.route_id == 1) {
      revoke_kernel_routes("elara.os.frame_authority", "elara.os.boot");
    }

    if (request.route_id == 2) {
      revoke_kernel_routes("elara.os.frame_authority", "elara.os.console_view");
    }

    if (request.route_id == 3) {
      revoke_kernel_routes("elara.os.frame_authority", "elara.os.window_manager");
    }

    if (request.route_id == 4) {
      revoke_kernel_routes("elara.os.registry", "elara.os.filesystem");
    }

    if (request.route_id == 5) {
      revoke_kernel_routes("elara.os.registry", "elara.os.block_io");
    }
  }

  host_signal();
}
