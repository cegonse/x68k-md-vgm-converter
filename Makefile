# Thin wrapper over CMake + scripts/init.sh + cest-runner. No build logic
# lives here; it only orchestrates. See CLAUDE.md and docs/.

BUILD_DIR    := build
EXTERNAL_DIR := external

CEST_HEADER  := $(EXTERNAL_DIR)/cest/cest
CEST_RUNNER  := $(EXTERNAL_DIR)/cest/cest-runner
XGMTOOL_BIN  := $(EXTERNAL_DIR)/xgmtool/build/xgmtool
VGM2TXT_BIN  := $(EXTERNAL_DIR)/vgmtools/build/vgm2txt

.PHONY: all init test clean ensure-init

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

# Clean build artifacts only; leave external/ intact (re-fetching is costly).
clean:
	rm -rf $(BUILD_DIR)
