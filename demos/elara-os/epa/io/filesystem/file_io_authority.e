#include "file_io_protocol.em"
#include "common/bytes.em"

type FileIoHandleState(
  int handle_id,
  int session_id,
  int mount_id,
  int path_hash,
  int inode_number,
  int flags,
  int cursor,
  int mode
) {
  return handle_id;
}

dynamic file_io_handles(FileIoHandleState, 8, 128, 16);

function int file_io_mode_owner_read() {
  return 256;
}

function int file_io_mode_group_read() {
  return 32;
}

function int file_io_mode_other_read() {
  return 4;
}

function int file_io_can_read(int session_id, int inode_mode) {
  if (session_id == file_io_session_system()) {
    return 1;
  }
  if (bit_and_i32(inode_mode, file_io_mode_other_read()) == file_io_mode_other_read()) {
    return 1;
  }
  return 0;
}

kernel(VM vm) {
  kernalId("elara.os.file_io");
  start_worker(file_io_ingress);
}

acl {
  "elara.os.shell" -> file_io_ingress;
  "elara.app.example" -> file_io_ingress;
}

worker file_io_ingress(FileIoRequest request) {
  static int next_handle_id;
  int handle_slot = 0 - 1;
  int handle_iter = dynamic_iterator(file_io_handles);
  int found_handle = 0;
  local FileIoHandleState handle;
  local FileIoResponse response;

  static {
    next_handle_id = 1;
  }

  response.opcode = request.opcode;
  response.request_id = request.request_id;
  response.status = file_io_status_not_ready();
  response.handle_id = request.handle_id;
  response.byte_count = 0;
  response.arg0 = 0;
  response.arg1 = 0;
  response.arg2 = 0;

  if (request.opcode == file_io_opcode_open()) {
    if (request.path_hash != 0) {
      if (bit_and_i32(request.flags, file_io_open_flag_read()) == file_io_open_flag_read()) {
        handle_slot = dyn_alloc(file_io_handles);
        handle.handle_id = next_handle_id;
        handle.session_id = request.session_id;
        handle.mount_id = request.mount_id;
        handle.path_hash = request.path_hash;
        handle.inode_number = 0;
        handle.flags = request.flags;
        handle.cursor = 0;
        handle.mode = file_io_mode_owner_read() + file_io_mode_group_read() + file_io_mode_other_read();
        file_io_handles[handle_slot] = handle;

        response.status = file_io_status_ok();
        response.handle_id = next_handle_id;
        next_handle_id = next_handle_id + 1;
      } else {
        response.status = file_io_status_permission_denied();
      }
    } else {
      response.status = file_io_status_not_found();
    }
  }

  if (request.opcode == file_io_opcode_read()) {
    while (FileIoHandleState existing = dynamic_next(handle_iter)) {
      if (existing.handle_id == request.handle_id) {
        found_handle = 1;
        if (file_io_can_read(existing.session_id, existing.mode) == 1) {
          response.status = file_io_status_not_ready();
          response.handle_id = existing.handle_id;
        } else {
          response.status = file_io_status_permission_denied();
        }
      }
    }
    if (found_handle == 0) {
      response.status = file_io_status_bad_handle();
    }
  }

  if (request.opcode == file_io_opcode_close()) {
    response.status = file_io_status_ok();
  }

  host_signal();
}
