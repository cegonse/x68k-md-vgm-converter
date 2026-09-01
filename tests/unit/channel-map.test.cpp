#include <cest>

#include <cstdint>

extern "C" {
#include <channel_map.h>
#include <error_code.h>
}

describe("ChannelMap", []() {
  it("maps a sorted keep-list positionally across both ports", []() {
    uint8_t keep[] = {0, 1, 2, 3, 5, 7};
    ChannelMap m;
    ErrorCode err = ERR_INTERNAL;
    expect(ChannelMap_Build(keep, 6, false, &m, &err)).toBeTruthy();
    expect((int)err).toBe((int)ERR_NONE);

    expect(m.target[0]).toBe(0);  expect(m.port[0]).toBe(0);
    expect(m.target[1]).toBe(1);  expect(m.port[1]).toBe(0);
    expect(m.target[2]).toBe(2);  expect(m.port[2]).toBe(0);
    expect(m.target[3]).toBe(3);  expect(m.port[3]).toBe(1);
    expect(m.target[5]).toBe(4);  expect(m.port[5]).toBe(1);
    expect(m.target[7]).toBe(5);  expect(m.port[7]).toBe(1);
    expect(m.target[4]).toBe(-1);
    expect(m.target[6]).toBe(-1);
  });

  it("sorts an unsorted keep-list before mapping", []() {
    uint8_t keep[] = {7, 0, 5};
    ChannelMap m;
    ErrorCode err = ERR_INTERNAL;
    expect(ChannelMap_Build(keep, 3, false, &m, &err)).toBeTruthy();
    expect(m.target[0]).toBe(0);
    expect(m.target[5]).toBe(1);
    expect(m.target[7]).toBe(2);
  });

  it("allows 5 channels with PCM and sets has_pcm", []() {
    uint8_t keep[] = {0, 1, 2, 3, 4};
    ChannelMap m;
    ErrorCode err = ERR_INTERNAL;
    expect(ChannelMap_Build(keep, 5, true, &m, &err)).toBeTruthy();
    expect(m.has_pcm).toBeTruthy();
    expect(m.target[4]).toBe(4);
    expect(m.port[4]).toBe(1);
  });

  it("rejects 6 channels when PCM is present", []() {
    uint8_t keep[] = {0, 1, 2, 3, 4, 5};
    ChannelMap m;
    ErrorCode err = ERR_NONE;
    expect(ChannelMap_Build(keep, 6, true, &m, &err)).toBeFalsy();
    expect((int)err).toBe((int)ERR_BAD_ARGS);
  });

  it("rejects more than 6 channels without PCM", []() {
    uint8_t keep[] = {0, 1, 2, 3, 4, 5, 6};
    ChannelMap m;
    ErrorCode err = ERR_NONE;
    expect(ChannelMap_Build(keep, 7, false, &m, &err)).toBeFalsy();
    expect((int)err).toBe((int)ERR_BAD_ARGS);
  });

  it("rejects duplicate indices", []() {
    uint8_t keep[] = {0, 1, 1};
    ChannelMap m;
    ErrorCode err = ERR_NONE;
    expect(ChannelMap_Build(keep, 3, false, &m, &err)).toBeFalsy();
    expect((int)err).toBe((int)ERR_BAD_ARGS);
  });

  it("rejects out-of-range indices", []() {
    uint8_t keep[] = {0, 8};
    ChannelMap m;
    ErrorCode err = ERR_NONE;
    expect(ChannelMap_Build(keep, 2, false, &m, &err)).toBeFalsy();
    expect((int)err).toBe((int)ERR_BAD_ARGS);
  });
});
