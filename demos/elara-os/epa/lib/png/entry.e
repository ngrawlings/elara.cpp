#include "png_protocol.em"
#include "../../registry/registry_protocol.em"
#include "../../dynamic_acl_protocol.em"

dynamic png_decode_states(PngDecodeState, 8, 64, 8);

kernel(VM vm) {
  kernalId("elara.lib.png.decoder");
  start_worker(png_decode_ingress);
}

acl {
  "elara.os.filesystem" -> png_decode_ingress;
  "elara.os.stream_io" -> png_decode_ingress;
  "elara.os.registry" -> png_decode_ingress;
  "elara.os.shell" -> png_decode_ingress;
  "elara.os.host" -> png_decode_ingress;
}

@attributes signal_mail_box_size:128
worker png_decode_ingress(PngDecodeRequest request) {
  static int registered;
  int slot = dyn_alloc(png_decode_states);
  int status = png_decode_status_ok();
  local PngDecodeState state;
  local RegistryRequest registry_request;
  local DynamicACLRequest acl_request;

  if (request.signature_lo != png_signature_lo()) {
    status = png_decode_status_bad_signature();
  }
  if (status == png_decode_status_ok()) {
    if (request.signature_hi != png_signature_hi()) {
      status = png_decode_status_bad_signature();
    }
  }
  if (status == png_decode_status_ok()) {
    if (request.input_size < 33) {
      status = png_decode_status_bad_ihdr();
    }
  }
  if (status == png_decode_status_ok()) {
    if (request.ihdr_length != 13) {
      status = png_decode_status_bad_ihdr();
    }
  }
  if (status == png_decode_status_ok()) {
    if (request.ihdr_type != png_chunk_ihdr()) {
      status = png_decode_status_bad_ihdr();
    }
  }
  if (status == png_decode_status_ok()) {
    if (request.width < 1) {
      status = png_decode_status_bad_dimensions();
    }
  }
  if (status == png_decode_status_ok()) {
    if (request.height < 1) {
      status = png_decode_status_bad_dimensions();
    }
  }
  if (status == png_decode_status_ok()) {
    if (request.bit_depth != 1) {
      if (request.bit_depth != 2) {
        if (request.bit_depth != 4) {
          if (request.bit_depth != 8) {
            if (request.bit_depth != 16) {
              status = png_decode_status_unsupported_format();
            }
          }
        }
      }
    }
  }
  if (status == png_decode_status_ok()) {
    if (request.color_type != 0) {
      if (request.color_type != 2) {
        if (request.color_type != 3) {
          if (request.color_type != 4) {
            if (request.color_type != 6) {
              status = png_decode_status_unsupported_format();
            }
          }
        }
      }
    }
  }
  if (status == png_decode_status_ok()) {
    if (request.bit_depth == 16) {
      if (request.color_type == 3) {
        status = png_decode_status_unsupported_format();
      }
    }
  }
  if (status == png_decode_status_ok()) {
    if (request.bit_depth == 4) {
      if (request.color_type == 2) {
        status = png_decode_status_unsupported_format();
      }
      if (request.color_type == 4) {
        status = png_decode_status_unsupported_format();
      }
      if (request.color_type == 6) {
        status = png_decode_status_unsupported_format();
      }
    }
  }
  if (status == png_decode_status_ok()) {
    if (request.bit_depth == 2) {
      if (request.color_type == 2) {
        status = png_decode_status_unsupported_format();
      }
      if (request.color_type == 4) {
        status = png_decode_status_unsupported_format();
      }
      if (request.color_type == 6) {
        status = png_decode_status_unsupported_format();
      }
    }
  }
  if (status == png_decode_status_ok()) {
    if (request.bit_depth == 1) {
      if (request.color_type == 2) {
        status = png_decode_status_unsupported_format();
      }
      if (request.color_type == 4) {
        status = png_decode_status_unsupported_format();
      }
      if (request.color_type == 6) {
        status = png_decode_status_unsupported_format();
      }
    }
  }
  if (status == png_decode_status_ok()) {
    if (request.color_type == 3) {
      if (request.bit_depth == 16) {
        status = png_decode_status_unsupported_format();
      }
    }
  }
  if (status == png_decode_status_ok()) {
    if (request.bit_depth == 0) {
      status = png_decode_status_unsupported_format();
    }
  }
  if (status == png_decode_status_ok()) {
    if (request.compression_method != 0) {
      status = png_decode_status_unsupported_format();
    }
  }
  if (status == png_decode_status_ok()) {
    if (request.filter_method != 0) {
      status = png_decode_status_unsupported_format();
    }
  }
  if (status == png_decode_status_ok()) {
    if (request.interlace_method < 0) {
      status = png_decode_status_unsupported_format();
    }
  }
  if (status == png_decode_status_ok()) {
    if (request.interlace_method > 1) {
      status = png_decode_status_unsupported_format();
    }
  }

  if (registered == 0) {
    registry_request.opcode = registry_request_register_authority();
    registry_request.path_hash_lo = png_registry_path_hash();
    registry_request.path_hash_hi = png_io_mime_hash();
    registry_request.flags = 1;

    acl_request.opcode = dynamic_acl_opcode_register();
    acl_request.route_id = png_registry_path_hash();
    acl_request.flags = dynamic_acl_authority_registry();
    acl_request.reserved = png_io_mime_hash();

    registered = 1;
  }

  state.request_id = request.request_id;
  state.status = status;
  state.width = request.width;
  state.height = request.height;
  state.bit_depth = request.bit_depth;
  state.color_type = request.color_type;
  state.registry_path_hash = png_registry_path_hash();
  state.io_mime_hash = png_io_mime_hash();
  png_decode_states[slot] = state;

  frame_begin(png_io_mime_hash(), status, request.request_id, request.width, request.height);
  frame_rect(request.color_type, request.bit_depth, request.compression_method, request.filter_method, request.interlace_method, 0, 0);
  host_signal();
}
