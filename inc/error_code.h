#pragma once

typedef enum ErrorCode {
  ERR_NONE = 0,
  ERR_BAD_ARGS = 1,
  ERR_OPEN_INPUT = 2,
  ERR_OPEN_OUTPUT = 3,
  ERR_PARSE = 4,
  ERR_UNSUPPORTED = 5,
  ERR_INTERNAL = 6
} ErrorCode;
