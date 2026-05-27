struct Packet;
struct Blob;

type Packet(int tag, int primitive_int) {
  int base = primitive_int + 1;
  return base + 41;
}

type Blob(int id) {
  return id;
}

kernel(VM vm) {
  kernalId("example.seed");
  worker_ingest(vm);
}

acl {
  "example.remote" -> worker_ingest;
}

worker worker_ingest(Packet packet) {
  switch (packet) {
    case 0:
      dispatch_packet(packet, 7);
      break;
    case 1:
      dispatch_packet(packet, 8);
      break;
    default:
      dispatch_packet(packet, 9);
      break;
  }
}

function int validate_packet(Packet packet) {
  return 1;
}

function int sum4(int a, int b, int c, int d) {
  int total = a + b;
  total = total + c;
  return total + d;
}

function int dispatch_packet(Packet packet, int z) {
  int valid = validate_packet(packet);
  if (z) {
    valid = valid + 10;
  } else if (packet) {
    valid = valid + 20;
  } else {
    valid = valid + 30;
  }
  int base = sum4(z, 2, 3, 4);
  return valid + base;
}

function int unchecked(Blob blob, int z) {
  return z;
}
