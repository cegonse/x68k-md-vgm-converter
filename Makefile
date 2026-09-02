# Thin wrapper over CMake + scripts/init.sh + cest-runner. No build logic
# lives here; it only orchestrates. See CLAUDE.md and docs/.

BUILD_DIR    := build
WEB_DIR      := web
EXTERNAL_DIR := external

TOOL_SOURCES := $(wildcard src/*.c)

CEST_HEADER  := $(EXTERNAL_DIR)/cest/cest
CEST_RUNNER  := $(EXTERNAL_DIR)/cest/cest-runner
XGMTOOL_BIN  := $(EXTERNAL_DIR)/xgmtool/build/xgmtool
VGM2TXT_BIN  := $(EXTERNAL_DIR)/vgmtools/build/vgm2txt

.PHONY: all init test clean ensure-init web

# Default: ensure external tools exist, then incrementally build the tool.
all: ensure-init
	cmake -S . -B $(BUILD_DIR)
	cmake --build $(BUILD_DIR)

# Set up external tools only. Idempotent.
init:
	scripts/init.sh

# Run init only when an expected artifact is missing, so warm trees stay fast.
ensure-init:
	@if [ ! -f "$(CEST_HEADER)" ] || [ ! -x "$(CEST_RUNNER)" ] || \
	    [ ! -x "$(XGMTOOL_BIN)" ] || [ ! -x "$(VGM2TXT_BIN)" ]; then \
	  echo "==> external tools missing or incomplete; running scripts/init.sh"; \
	  scripts/init.sh; \
	else \
	  echo "==> external tools present"; \
	fi

# Incrementally build the tool + test binaries, then run every test_* in the
# build dir. Non-zero exit if any test fails.
test: ensure-init
	cmake -S . -B $(BUILD_DIR)
	cmake --build $(BUILD_DIR)
	cmake --build $(BUILD_DIR) --target build_tests
	"$(CEST_RUNNER)" $(BUILD_DIR)/

# Build the WebAssembly converter into build/web/ and assemble a servable
# directory there (WASM glue + the page source from web/). The C core is reused
# verbatim (main.c drives App_Run); the page calls it over Emscripten's virtual
# FS. web/ holds only source; all generated artifacts live under build/web/.
# Requires an Emscripten toolchain (emcc) in PATH.
web:
	@command -v emcc >/dev/null 2>&1 || { \
	  echo "error: emcc not found; install/activate the Emscripten SDK" >&2; exit 1; }
	mkdir -p $(BUILD_DIR)/web
	emcc $(TOOL_SOURCES) -Iinc -std=c99 -O2 -sUSE_ZLIB=1 \
	  -sMODULARIZE=1 -sEXPORT_NAME=createVgmModule \
	  -sEXPORTED_RUNTIME_METHODS=callMain,FS \
	  -sINVOKE_RUN=0 -sEXIT_RUNTIME=0 -sALLOW_MEMORY_GROWTH=1 \
	  -o $(BUILD_DIR)/web/vgmconv.js
	cp $(WEB_DIR)/index.html $(BUILD_DIR)/web/index.html
	@echo "==> web build ready: serve $(BUILD_DIR)/web/ over HTTP (e.g. python3 -m http.server -d $(BUILD_DIR)/web)"

# Clean build artifacts only; leave external/ intact (re-fetching is costly).
clean:
	rm -rf $(BUILD_DIR)
