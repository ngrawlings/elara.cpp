//
//  BRpcTests.h
//  UnitTests
//

#ifndef BRpcTests_h
#define BRpcTests_h

bool brpc_roundtrip_byte();
bool brpc_roundtrip_short();
bool brpc_roundtrip_int();
bool brpc_roundtrip_long();
bool brpc_roundtrip_string();
bool brpc_roundtrip_empty_string();
bool brpc_roundtrip_named_byte();
bool brpc_roundtrip_named_short();
bool brpc_roundtrip_named_int();
bool brpc_roundtrip_named_long();
bool brpc_roundtrip_named_string();
bool brpc_roundtrip_array_unnamed();
bool brpc_roundtrip_object();
bool brpc_roundtrip_nested_arrays();
bool brpc_roundtrip_empty_array();
bool brpc_roundtrip_mixed_object();
bool brpc_sequential_reads();
bool brpc_skip_value();
bool brpc_boundary_values();
bool brpc_truncated_input();
bool brpc_wrong_type_rejected();
bool brpc_wire_layout_byte();
bool brpc_wire_layout_array_header();

#endif  // BRpcTests_h
