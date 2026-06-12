#ifndef ELARA_OS_FILE_IO_PROTOCOL_EM
#define ELARA_OS_FILE_IO_PROTOCOL_EM

type FileIoRequest(
  int opcode,
  int request_id,
  int session_id,
  int mount_id,
  int path_hash,
  int handle_id,
  int offset,
  int byte_count,
  int flags
) {
  return opcode;
}

type FileIoResponse(
  int opcode,
  int request_id,
  int status,
  int handle_id,
  int byte_count,
  int arg0,
  int arg1,
  int arg2
) {
  return opcode;
}

function int file_io_opcode_open() {
  return 1;
}

function int file_io_opcode_read() {
  return 2;
}

function int file_io_opcode_close() {
  return 3;
}

function int file_io_status_ok() {
  return 0;
}

function int file_io_status_not_found() {
  return 0 - 2;
}

function int file_io_status_permission_denied() {
  return 0 - 13;
}

function int file_io_status_bad_handle() {
  return 0 - 81;
}

function int file_io_status_not_ready() {
  return 0 - 115;
}

function int file_io_open_flag_read() {
  return 1;
}

function int file_io_open_flag_write() {
  return 2;
}

function int file_io_session_system() {
  return 0;
}

#endif
