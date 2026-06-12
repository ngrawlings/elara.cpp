#ifndef ELARA_OS_SHELL_BOOT_PROTOCOL_EM
#define ELARA_OS_SHELL_BOOT_PROTOCOL_EM

type ShellProcessImage(byte[] payload) {
  return payload;
}

type ShellPngLibraryImage(byte[] payload) {
  return payload;
}

type ShellBootLogoHeader(
  int request_id,
  int input_size,
  int signature_lo,
  int signature_hi,
  int ihdr_length,
  int ihdr_type,
  int width,
  int height,
  int bit_depth,
  int color_type,
  int compression_method,
  int filter_method,
  int interlace_method
) {
  return request_id;
}

#endif
