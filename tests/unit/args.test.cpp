#include <cest>

#include <string>

extern "C" {
#include <args.h>
#include <error_code.h>
}

static char P[] = "x68k-md-vgm-conv";

describe("Args", []() {
  it("parses positional input and output", []() {
    char in[] = "a.vgm";
    char out[] = "b.vgm";
    char *argv[] = {P, in, out};
    Args args;
    ErrorCode err = ERR_INTERNAL;
    expect(Args_Parse(3, argv, &args, &err)).toBeTruthy();
    expect((int)err).toBe((int)ERR_NONE);
    expect(std::string(args.input)).toBe(std::string("a.vgm"));
    expect(std::string(args.output)).toBe(std::string("b.vgm"));
    expect(args.fm_channel_count).toBe(-1);
  });

  it("parses --fm-channels list", []() {
    char in[] = "a.vgm";
    char out[] = "b.vgm";
    char flag[] = "--fm-channels";
    char list[] = "0,1,2,3,5,7";
    char *argv[] = {P, in, out, flag, list};
    Args args;
    ErrorCode err = ERR_INTERNAL;
    expect(Args_Parse(5, argv, &args, &err)).toBeTruthy();
    expect(args.fm_channel_count).toBe(6);
    expect(args.fm_channels[0]).toBe((uint8_t)0);
    expect(args.fm_channels[4]).toBe((uint8_t)5);
    expect(args.fm_channels[5]).toBe((uint8_t)7);
  });

  it("parses the --fm-channels=list form", []() {
    char in[] = "a.vgm";
    char out[] = "b.vgm";
    char flag[] = "--fm-channels=0,1,2";
    char *argv[] = {P, in, out, flag};
    Args args;
    ErrorCode err = ERR_INTERNAL;
    expect(Args_Parse(4, argv, &args, &err)).toBeTruthy();
    expect(args.fm_channel_count).toBe(3);
  });

  it("fails when output is missing", []() {
    char in[] = "a.vgm";
    char *argv[] = {P, in};
    Args args;
    ErrorCode err = ERR_NONE;
    expect(Args_Parse(2, argv, &args, &err)).toBeFalsy();
    expect((int)err).toBe((int)ERR_BAD_ARGS);
  });

  it("fails when --fm-channels has no value", []() {
    char in[] = "a.vgm";
    char out[] = "b.vgm";
    char flag[] = "--fm-channels";
    char *argv[] = {P, in, out, flag};
    Args args;
    ErrorCode err = ERR_NONE;
    expect(Args_Parse(4, argv, &args, &err)).toBeFalsy();
    expect((int)err).toBe((int)ERR_BAD_ARGS);
  });

  it("fails on an unknown option", []() {
    char in[] = "a.vgm";
    char out[] = "b.vgm";
    char flag[] = "--nope";
    char *argv[] = {P, in, out, flag};
    Args args;
    ErrorCode err = ERR_NONE;
    expect(Args_Parse(4, argv, &args, &err)).toBeFalsy();
    expect((int)err).toBe((int)ERR_BAD_ARGS);
  });

  it("fails on a malformed channel list", []() {
    char in[] = "a.vgm";
    char out[] = "b.vgm";
    char flag[] = "--fm-channels=0,x";
    char *argv[] = {P, in, out, flag};
    Args args;
    ErrorCode err = ERR_NONE;
    expect(Args_Parse(4, argv, &args, &err)).toBeFalsy();
    expect((int)err).toBe((int)ERR_BAD_ARGS);
  });
});
