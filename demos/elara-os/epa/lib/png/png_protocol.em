#ifndef ELARA_OS_LIB_PNG_PROTOCOL_EM
#define ELARA_OS_LIB_PNG_PROTOCOL_EM

type PngDecodeRequest(
  int opcode,
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
  return opcode;
}

type PngDecodeState(
  int request_id,
  int status,
  int width,
  int height,
  int bit_depth,
  int color_type,
  int registry_path_hash,
  int io_mime_hash
) {
  return request_id;
}

function int png_decode_opcode_probe() {
  return 1;
}

function int png_decode_opcode_decode_header() {
  return 2;
}

function int png_decode_status_ok() {
  return 1;
}

function int png_decode_status_bad_signature() {
  return 0 - 1;
}

function int png_decode_status_bad_ihdr() {
  return 0 - 2;
}

function int png_decode_status_bad_dimensions() {
  return 0 - 3;
}

function int png_decode_status_unsupported_format() {
  return 0 - 4;
}

function int png_signature_lo() {
  return 1196314761;
}

function int png_signature_hi() {
  return 169478669;
}

function int png_chunk_ihdr() {
  return 1380206665;
}

function int png_registry_path_hash() {
  return 704726087;
}

function int png_io_mime_hash() {
  return 0 - 1341472966;
}

#endif
