// -*- mode:C++; tab-width:8; c-basic-offset:2; indent-tabs-mode:t -*-
// vim: ts=8 sw=2 smarttab

#pragma once

#include "key_layout.h"
#include "crimson/os/seastore/onode_manager/staged-fltree/node_types.h"

namespace crimson::os::seastore::onode {

class NodeExtentMutable;

struct node_header_t {
  static constexpr unsigned FIELD_TYPE_BITS = 6u;
  static_assert(static_cast<uint8_t>(field_type_t::_MAX) <= 1u << FIELD_TYPE_BITS);
  static constexpr unsigned NODE_TYPE_BITS = 1u;
  static constexpr unsigned B_LEVEL_TAIL_BITS = 1u;
  using bits_t = uint8_t;

  node_header_t() {}
  std::optional<field_type_t> get_field_type() const {
    if (field_type >= FIELD_TYPE_MAGIC &&
        field_type < static_cast<uint8_t>(field_type_t::_MAX)) {
      return static_cast<field_type_t>(field_type);
    } else {
      return std::nullopt;
    }
  }
  node_type_t get_node_type() const {
    return static_cast<node_type_t>(node_type);
  }
  bool get_is_level_tail() const {
    return is_level_tail;
  }

  static void bootstrap_extent(
      NodeExtentMutable&, field_type_t, node_type_t, bool, level_t);

  static void update_is_level_tail(NodeExtentMutable&, const node_header_t&, bool);

  bits_t field_type : FIELD_TYPE_BITS;
  bits_t node_type : NODE_TYPE_BITS;
  bits_t is_level_tail : B_LEVEL_TAIL_BITS;
  static_assert(sizeof(bits_t) * 8 ==
                FIELD_TYPE_BITS + NODE_TYPE_BITS + B_LEVEL_TAIL_BITS);
  level_t level;

 private:
  void set_field_type(field_type_t type) {
    field_type = static_cast<uint8_t>(type);
  }
  void set_node_type(node_type_t type) {
    node_type = static_cast<uint8_t>(type);
  }
  void set_is_level_tail(bool value) {
    is_level_tail = static_cast<uint8_t>(value);
  }
} __attribute__((packed));

template <typename FixedKeyType, field_type_t _FIELD_TYPE>
struct _slot_t {
  using key_t = FixedKeyType;
  static constexpr field_type_t FIELD_TYPE = _FIELD_TYPE;
  static constexpr node_offset_t OVERHEAD = sizeof(_slot_t) - sizeof(key_t);

  key_t key;
  node_offset_t right_offset;
} __attribute__((packed));
using slot_0_t = _slot_t<shard_pool_crush_t, field_type_t::N0>;
using slot_1_t = _slot_t<crush_t, field_type_t::N1>;
using slot_3_t = _slot_t<snap_gen_t, field_type_t::N3>;

struct node_range_t {
  node_offset_t start;
  node_offset_t end;
};

template <typename FieldType>
const char* fields_start(const FieldType& node) {
  return reinterpret_cast<const char*>(&node);
}

template <node_type_t NODE_TYPE, typename FieldType>
node_range_t fields_free_range_before(
    const FieldType& node, size_t index) {
  assert(index <= node.num_keys);
  node_offset_t offset_start = node.get_key_start_offset(index);
  node_offset_t offset_end =
    (index == 0 ? FieldType::SIZE
                   : node.get_item_start_offset(index - 1));
  if constexpr (NODE_TYPE == node_type_t::INTERNAL) {
    if (node.is_level_tail() && index == node.num_keys) {
      offset_end -= sizeof(laddr_t);
    }
  }
  assert(offset_start <= offset_end);
  assert(offset_end - offset_start < FieldType::SIZE);
  return {offset_start, offset_end};
}

/**
 * _node_fields_013_t (node_fields_0_t, node_fields_1_t, leaf_fields_3_t
 *
 * The STAGE_LEFT layout implementation for node N0/N1, or the STAGE_RIGHT
 * layout implementation for leaf node N3.
 *
 * The node layout storing n slots:
 *
 * # <----------------------------- node range --------------------------------------> #
 * #                                                 #<~># free space                  #
 * # <----- left part -----------------------------> # <~# <----- right slots -------> #
 * #               # <---- left slots -------------> #~> #                             #
 * #               #                slots [2, n) |<~>#   #<~>| right slots [2, n)      #
 * #               # <- slot 0 -> | <- slot 1 -> |   #   #   | <-- s1 --> | <-- s0 --> #
 * #               #              |              |   #   #   |            |            #
 * #        | num_ #     | right  |     | right  |   #   #   | next-stage | next-stage #
 * # header | keys # key | offset | key | offset |   #   #   | container  | container  #
 * #        |      # 0   | 0      | 1   | 1      |...#...#...| or onode 1 | or onode 0 #
 *                           |              |                ^            ^
 *                           |              |                |            |
 *                           |              +----------------+            |
 *                           +--------------------------------------------+
 */
template <typename SlotType>
struct _node_fields_013_t {
  // TODO: decide by NODE_BLOCK_SIZE, sizeof(SlotType), sizeof(laddr_t)
  // and the minimal size of variable_key.
  using num_keys_t = uint8_t;
  using key_t = typename SlotType::key_t;
  using key_get_type = const key_t&;
  using me_t = _node_fields_013_t<SlotType>;
  static constexpr field_type_t FIELD_TYPE = SlotType::FIELD_TYPE;
  static constexpr node_offset_t SIZE = NODE_BLOCK_SIZE;
  static constexpr node_offset_t HEADER_SIZE =
    sizeof(node_header_t) + sizeof(num_keys_t);
  static constexpr node_offset_t ITEM_OVERHEAD = SlotType::OVERHEAD;

  bool is_level_tail() const { return header.get_is_level_tail(); }
  node_offset_t total_size() const { return SIZE; }
  key_get_type get_key(size_t index) const {
    assert(index < num_keys);
    return slots[index].key;
  }
  node_offset_t get_key_start_offset(size_t index) const {
    assert(index <= num_keys);
    auto offset = HEADER_SIZE + sizeof(SlotType) * index;
    assert(offset < SIZE);
    return offset;
  }
  node_offset_t get_item_start_offset(size_t index) const {
    assert(index < num_keys);
    auto offset = slots[index].right_offset;
    assert(offset <= SIZE);
    return offset;
  }
  const void* p_offset(size_t index) const {
    assert(index < num_keys);
    return &slots[index].right_offset;
  }
  node_offset_t get_item_end_offset(size_t index) const {
    return index == 0 ? SIZE : get_item_start_offset(index - 1);
  }
  template <node_type_t NODE_TYPE>
  node_offset_t free_size_before(size_t index) const {
    auto range = fields_free_range_before<NODE_TYPE>(*this, index);
    return range.end - range.start;
  }

  static node_offset_t estimate_insert_one() { return sizeof(SlotType); }
  template <KeyT KT>
  static void insert_at(
      NodeExtentMutable&, const full_key_t<KT>& key,
      const me_t& node, size_t index, node_offset_t size_right);
  static void update_size_at(
      NodeExtentMutable&, const me_t& node, size_t index, int change);
  static void append_key(
      NodeExtentMutable&, const key_t& key, char*& p_append);
  template <KeyT KT>
  static void append_key(
      NodeExtentMutable& mut, const full_key_t<KT>& key, char*& p_append) {
    append_key(mut, key_t::template from_key<KT>(key), p_append);
  }
  static void append_offset(
      NodeExtentMutable& mut, node_offset_t offset_to_right, char*& p_append);

  node_header_t header;
  num_keys_t num_keys = 0u;
  SlotType slots[];
} __attribute__((packed));
using node_fields_0_t = _node_fields_013_t<slot_0_t>;
using node_fields_1_t = _node_fields_013_t<slot_1_t>;

/**
 * node_fields_2_t
 *
 * The STAGE_STRING layout implementation for node N2.
 *
 * The node layout storing n slots:
 *
 * # <--------------------------------- node range ----------------------------------------> #
 * #                                     #<~># free space                                    #
 * # <------- left part ---------------> # <~# <--------- right slots ---------------------> #
 * #               # <---- offsets ----> #~> #<~>| slots [2, n)                              #
 * #               #  offsets [2, n) |<~>#   #   | <----- slot 1 ----> | <----- slot 0 ----> #
 * #               #                 |   #   #   |                     |                     #
 * #        | num_ # offset | offset |   #   #   | next-stage | ns-oid | next-stage | ns-oid #
 * # header | keys # 0      | 1      |...#...#...| container1 | 1      | container0 | 0      #
 *                     |        |                ^                     ^
 *                     |        |                |                     |
 *                     |        +----------------+                     |
 *                     +-----------------------------------------------+
 */
struct node_fields_2_t {
  // TODO: decide by NODE_BLOCK_SIZE, sizeof(node_off_t), sizeof(laddr_t)
  // and the minimal size of variable_key.
  using num_keys_t = uint8_t;
  using key_t = ns_oid_view_t;
  using key_get_type = key_t;
  static constexpr field_type_t FIELD_TYPE = field_type_t::N2;
  static constexpr node_offset_t SIZE = NODE_BLOCK_SIZE;
  static constexpr node_offset_t HEADER_SIZE =
    sizeof(node_header_t) + sizeof(num_keys_t);
  static constexpr node_offset_t ITEM_OVERHEAD = sizeof(node_offset_t);

  bool is_level_tail() const { return header.get_is_level_tail(); }
  node_offset_t total_size() const { return SIZE; }
  key_get_type get_key(size_t index) const {
    assert(index < num_keys);
    node_offset_t item_end_offset =
      (index == 0 ? SIZE : offsets[index - 1]);
    assert(item_end_offset <= SIZE);
    const char* p_start = fields_start(*this);
    return key_t(p_start + item_end_offset);
  }
  node_offset_t get_key_start_offset(size_t index) const {
    assert(index <= num_keys);
    auto offset = HEADER_SIZE + sizeof(node_offset_t) * num_keys;
    assert(offset <= SIZE);
    return offset;
  }
  node_offset_t get_item_start_offset(size_t index) const {
    assert(index < num_keys);
    auto offset = offsets[index];
    assert(offset <= SIZE);
    return offset;
  }
  const void* p_offset(size_t index) const {
    assert(index < num_keys);
    return &offsets[index];
  }
  node_offset_t get_item_end_offset(size_t index) const {
    return index == 0 ? SIZE : get_item_start_offset(index - 1);
  }
  template <node_type_t NODE_TYPE>
  node_offset_t free_size_before(size_t index) const {
    auto range = fields_free_range_before<NODE_TYPE>(*this, index);
    return range.end - range.start;
  }

  static node_offset_t estimate_insert_one() { return sizeof(node_offset_t); }
  template <KeyT KT>
  static void insert_at(
      NodeExtentMutable& mut, const full_key_t<KT>& key,
      const node_fields_2_t& node, size_t index, node_offset_t size_right) {
    ceph_abort("not implemented");
  }
  static void update_size_at(
      NodeExtentMutable& mut, const node_fields_2_t& node, size_t index, int change) {
    ceph_abort("not implemented");
  }
  static void append_key(
      NodeExtentMutable& mut, const key_t& key, char*& p_append) {
    ns_oid_view_t::append(mut, key, p_append);
  }
  template <KeyT KT>
  static void append_key(
      NodeExtentMutable& mut, const full_key_t<KT>& key, char*& p_append) {
    ns_oid_view_t::append<KT>(mut, key, p_append);
  }
  static void append_offset(
      NodeExtentMutable& mut, node_offset_t offset_to_right, char*& p_append);

  node_header_t header;
  num_keys_t num_keys = 0u;
  node_offset_t offsets[];
} __attribute__((packed));

/**
 * internal_fields_3_t
 *
 * The STAGE_RIGHT layout implementation for N2.
 *
 * The node layout storing 3 children:
 *
 * # <---------------- node range ---------------------------> #
 * #               # <-- keys ---> # <---- laddrs -----------> #
 * #  free space:  #           |<~>#                       |<~>#
 * #               #           |   #                       |   #
 * #        | num_ # key | key |   # laddr | laddr | laddr |   #
 * # header | keys # 0   | 1   |...# 0     | 1     | 2     |...#
 */
// TODO: decide by NODE_BLOCK_SIZE, sizeof(snap_gen_t), sizeof(laddr_t)
static constexpr unsigned MAX_NUM_KEYS_I3 = 170u;
template <unsigned MAX_NUM_KEYS>
struct _internal_fields_3_t {
  using key_get_type = const snap_gen_t&;
  using me_t = _internal_fields_3_t<MAX_NUM_KEYS>;
  // TODO: decide by NODE_BLOCK_SIZE, sizeof(snap_gen_t), sizeof(laddr_t)
  using num_keys_t = uint8_t;
  static constexpr field_type_t FIELD_TYPE = field_type_t::N3;
  static constexpr node_offset_t SIZE = sizeof(me_t);
  static constexpr node_offset_t HEADER_SIZE =
    sizeof(node_header_t) + sizeof(num_keys_t);
  static constexpr node_offset_t ITEM_OVERHEAD = 0u;

  bool is_level_tail() const { return header.get_is_level_tail(); }
  node_offset_t total_size() const {
    if (is_level_tail()) {
      return SIZE - sizeof(snap_gen_t);
    } else {
      return SIZE;
    }
  }
  key_get_type get_key(size_t index) const {
    assert(index < num_keys);
    return keys[index];
  }
  template <node_type_t NODE_TYPE>
  std::enable_if_t<NODE_TYPE == node_type_t::INTERNAL, node_offset_t>
  free_size_before(size_t index) const {
    assert(index <= num_keys);
    assert(num_keys <= (is_level_tail() ? MAX_NUM_KEYS - 1 : MAX_NUM_KEYS));
    auto free = (MAX_NUM_KEYS - index) * (sizeof(snap_gen_t) + sizeof(laddr_t));
    if (is_level_tail() && index == num_keys) {
      free -= (sizeof(snap_gen_t) + sizeof(laddr_t));
    }
    assert(free < SIZE);
    return free;
  }

  static node_offset_t estimate_insert_one() {
    return sizeof(snap_gen_t) + sizeof(laddr_t);
  }
  template <KeyT KT>
  static void insert_at(
      NodeExtentMutable& mut, const full_key_t<KT>& key,
      const me_t& node, size_t index, node_offset_t size_right) {
    ceph_abort("not implemented");
  }
  static void update_size_at(
      NodeExtentMutable& mut, const me_t& node, size_t index, int change) {
    ceph_abort("not implemented");
  }

  node_header_t header;
  num_keys_t num_keys = 0u;
  snap_gen_t keys[MAX_NUM_KEYS];
  laddr_packed_t child_addrs[MAX_NUM_KEYS];
} __attribute__((packed));
static_assert(_internal_fields_3_t<MAX_NUM_KEYS_I3>::SIZE <= NODE_BLOCK_SIZE &&
              _internal_fields_3_t<MAX_NUM_KEYS_I3 + 1>::SIZE > NODE_BLOCK_SIZE);
using internal_fields_3_t = _internal_fields_3_t<MAX_NUM_KEYS_I3>;

using leaf_fields_3_t = _node_fields_013_t<slot_3_t>;

}
