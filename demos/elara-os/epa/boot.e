#include "frame_protocol.em"
#include "storage_protocol.em"

type BootDeviceList(
  int version,
  int device_count,
  int payload_size,
  byte[] payload
) {
  return version;
}

type BootAssets(int version, int flags) {
  return version;
}

kernel(VM vm) {
  kernalId("elara.os.boot");
  start_worker(boot_ingress);
  start_worker(publish_boot_assets);
}

acl {
  "elara.os.host" -> boot_ingress;
  "elara.os.security" -> boot_ingress;
}

worker boot_ingress(BootDeviceList trigger) {
  local BootDeviceList staged;
  local FrameBoot boot_frame;
  local BlockDeviceRegistration drive;
  local FileSystemMount mount;
  local byte[64] mount_path;
  int device_count = 0;
  int payload_size = 0;
  int blob_version = 0;
  int blob_count = 0;
  int blob_cursor = 0;
  int device_index = 0;
  int drive_id = 0;
  int mount_size = 0;
  int mount_id = 0;
  int mount_flags = 0;
  int mount_words = 0;
  int mount_word_index = 0;

  boot_frame.phase = 1;
  boot_frame.flags = 0;
  far_signal("elara.os.frame_authority", publish_boot_frame, boot_frame);

  staged.version = trigger.version;
  staged.device_count = trigger.device_count;
  staged.payload_size = trigger.payload_size;
  staged.payload = trigger.payload;

  device_count = staged.device_count;
  payload_size = staged.payload_size;
  blob_version = local_load_i32(staged.payload, 0);
  blob_count = local_load_i32(staged.payload, 4);
  blob_cursor = 8;

  if (blob_version == staged.version) {
    if (blob_count < device_count) {
      device_count = blob_count;
    }
  }

  while (device_index < device_count) {
    drive_id = local_load_i32(staged.payload, blob_cursor + 0);
    mount_size = local_load_i32(staged.payload, blob_cursor + 4);
    mount_id = device_index + 1;
    mount_flags = 0;
    mount_words = (mount_size + 3) / 4;
    mount_word_index = 0;

    while (mount_word_index < mount_words) {
      local_store_i32(
        mount_path,
        mount_word_index * 4,
        local_load_i32(staged.payload, blob_cursor + 8 + (mount_word_index * 4))
      );
      mount_word_index = mount_word_index + 1;
    }

    if (mount_size == 1) {
      if (bit_and_i32(local_load_i32(mount_path, 0), 255) == 47) {
        mount_flags = 1;
      }
    }

    drive.drive_id = drive_id;
    drive.block_size = 4096;
    drive.block_count = 0;
    drive.flags = mount_flags;
    drive.mount_id = mount_id;
    drive.mount_size = mount_size;
    drive.mount_data = mount_path;
    far_signal("elara.os.block_io", register_block_device, drive);

    mount.mount_id = mount_id;
    mount.drive_id = drive_id;
    mount.fs_kind = 1;
    mount.flags = mount_flags;
    mount.mount_size = mount_size;
    mount.mount_data = mount_path;
    far_signal("elara.os.filesystem", register_mount, mount);

    blob_cursor = blob_cursor + 8 + (mount_words * 4);
    if (blob_cursor > payload_size) {
      device_index = device_count;
    } else {
      device_index = device_index + 1;
    }
  }

  next publish_boot_assets;
}

worker publish_boot_assets(BootDeviceList trigger) {
  static BootAssets assets;

  static {
    assets.version = 1;
    assets.flags = 0;
    rgm_publish("elara.os.boot.assets", assets);
  }
}
