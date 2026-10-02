DMOD_CPU_FAMILY ?= stm32f7
BUILD_DIR ?= build-$(DMOD_CPU_FAMILY)
CMAKE_ARGS ?=
ifneq ($(strip $(DMOD_DIR)),)
CMAKE_ARGS += -DDMOD_DIR=$(DMOD_DIR)
endif
.PHONY: all configure clean test
all: configure
	cmake --build $(BUILD_DIR) --parallel 2
configure:
	cmake -S . -B $(BUILD_DIR) -DDMOD_CPU_FAMILY=$(DMOD_CPU_FAMILY) $(CMAKE_ARGS)
clean:
	cmake --build $(BUILD_DIR) --target clean
test:
	cmake -S tests/host -B build-host
	cmake --build build-host --parallel 2
	ctest --test-dir build-host --output-on-failure
