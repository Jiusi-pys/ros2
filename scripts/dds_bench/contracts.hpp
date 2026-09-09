// Copyright 2026 Yusheng Peng. SPDX-License-Identifier: Apache-2.0
#pragma once
#include <cstdint>
#include <chrono>
#include <stdexcept>
#include <string>
#include <vector>
namespace bench {
constexpr uint64_t MAX_BYTES = 4ULL * 1024 * 1024;
constexpr size_t HEADER = 40;
constexpr uint64_t MAGIC = 0x3148434e45425344ULL;
inline std::string peer_topic(const std::string &topic) {
  if(topic.size()<3) throw std::invalid_argument("duplex topic suffix");
  auto suffix=topic.substr(topic.size()-3);
  if(suffix!="_ab" && suffix!="_ba") throw std::invalid_argument("duplex topic suffix");
  return topic.substr(0,topic.size()-2)+(suffix=="_ab"?"ba":"ab");
}
inline std::chrono::steady_clock::time_point sample_deadline(
  std::chrono::steady_clock::time_point sent, uint64_t timeout_ms) {
  return sent + std::chrono::milliseconds(timeout_ms);
}
inline void put(std::vector<uint8_t> &v, size_t off, uint64_t n) {
  for (size_t i = 0; i < 8; ++i) v.at(off+i) = static_cast<uint8_t>(n >> (i*8));
}
inline uint64_t get(const std::vector<uint8_t> &v, size_t off) {
  uint64_t n = 0;
  for (size_t i = 0; i < 8; ++i) n |= uint64_t(v.at(off+i)) << (i*8);
  return n;
}
inline uint64_t checksum(const std::vector<uint8_t> &v) {
  uint64_t h = 14695981039346656037ULL;
  for (size_t i = HEADER; i < v.size(); ++i) { h ^= v[i]; h *= 1099511628211ULL; }
  return h;
}
inline std::vector<uint8_t> payload(uint64_t size, uint64_t run) {
  if (size == 0 || size > MAX_BYTES) throw std::invalid_argument("payload range");
  std::vector<uint8_t> v(size + HEADER);
  uint64_t x = run ? run : 1;
  for (size_t i = HEADER; i < v.size(); ++i) {
    x ^= x << 13; x ^= x >> 7; x ^= x << 17; v[i] = uint8_t(x);
  }
  put(v, 0, MAGIC); put(v, 8, run); put(v, 24, size); put(v, 32, checksum(v));
  return v;
}
inline void stamp(std::vector<uint8_t> &v, uint64_t seq, uint64_t run) {
  put(v, 8, run); put(v, 16, seq);
}
inline uint64_t sequence(const std::vector<uint8_t> &v) { return get(v, 16); }
inline bool valid(const std::vector<uint8_t> &v, uint64_t size, uint64_t run) {
  return size <= MAX_BYTES && v.size() == size+HEADER && get(v, 0) == MAGIC &&
         get(v, 8) == run && get(v, 24) == size && get(v, 32) == checksum(v);
}
}
