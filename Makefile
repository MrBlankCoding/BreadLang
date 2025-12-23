.PHONY: help configure build all clean distclean rebuild test test-all test-property install run jit compile-llvm compile-obj compile-exe package-macos

BUILD_DIR ?= build
BUILD_TYPE ?= Debug
BREADLANG ?= $(BUILD_DIR)/breadlang

# macOS release bundle settings (only used by package-macos)
MACOS_PACKAGE_BUILD_DIR ?= build-macos-universal
MACOS_PACKAGE_DIST_DIR ?= dist/macos-universal
MACOS_DEPLOYMENT_TARGET ?= 15.0
MACOS_PACKAGE_ARCHS ?= $(shell uname -m)

help:
	@printf "%s\n" "Build Targets:" \
		"  make / make build      Build (uses CMake)" \
		"  make configure         Configure $(BUILD_DIR) (BUILD_TYPE=$(BUILD_TYPE))" \
		"  make clean             CMake-level clean in $(BUILD_DIR)" \
		"  make distclean         Remove $(BUILD_DIR)" \
		"  make rebuild           distclean + configure + build" \
		"" \
		"Execution Targets:" \
		"  make run FILE=<file>   Run BreadLang file with JIT compilation" \
		"  make jit FILE=<file>   Run BreadLang file with JIT compilation (alias for run)" \
		"" \
		"Compilation Targets:" \
		"  make compile-exe FILE=<file> [OUT=<output>]   Compile to executable" \
		"  make compile-llvm FILE=<file> [OUT=<output>]  Compile to LLVM IR" \
		"  make compile-obj FILE=<file> [OUT=<output>]   Compile to object file" \
		"" \
		"Test Targets:" \
		"  make test              Run CTest" \
		"  make test-all          Run custom test-all target" \
		"  make test-property     Run property-based tests" \
		"" \
		"Other Targets:" \
		"  make install           Run install (set DESTDIR/PREFIX via CMake)"

configure:
	cmake -S . -B $(BUILD_DIR) -DCMAKE_BUILD_TYPE=$(BUILD_TYPE)

build: configure
	cmake --build $(BUILD_DIR)

all: build

clean:
	cmake --build $(BUILD_DIR) --target clean

distclean:
	rm -rf $(BUILD_DIR)

rebuild: distclean build

# Execution targets
run: build
	@if [ -z "$(FILE)" ]; then \
		echo "Error: FILE parameter required. Usage: make run FILE=program.bread"; \
		exit 1; \
	fi
	$(BREADLANG) --jit "$(FILE)"

jit: run

# Compilation targets
compile-exe: build
	@if [ -z "$(FILE)" ]; then \
		echo "Error: FILE parameter required. Usage: make compile-exe FILE=program.bread [OUT=output]"; \
		exit 1; \
	fi
	@OUT_FILE="$(if $(OUT),$(OUT),$(basename $(FILE)))" && \
	$(BREADLANG) --emit-exe -o "$$OUT_FILE" "$(FILE)" && \
	echo "Executable created: $$OUT_FILE"

compile-llvm: build
	@if [ -z "$(FILE)" ]; then \
		echo "Error: FILE parameter required. Usage: make compile-llvm FILE=program.bread [OUT=output.ll]"; \
		exit 1; \
	fi
	@OUT_FILE="$(if $(OUT),$(OUT),$(basename $(FILE)).ll)" && \
	$(BREADLANG) --emit-llvm -o "$$OUT_FILE" "$(FILE)" && \
	echo "LLVM IR created: $$OUT_FILE"

compile-obj: build
	@if [ -z "$(FILE)" ]; then \
		echo "Error: FILE parameter required. Usage: make compile-obj FILE=program.bread [OUT=output.o]"; \
		exit 1; \
	fi
	@OUT_FILE="$(if $(OUT),$(OUT),$(basename $(FILE)).o)" && \
	$(BREADLANG) --emit-obj -o "$$OUT_FILE" "$(FILE)" && \
	echo "Object file created: $$OUT_FILE"

test: build
	ctest --test-dir $(BUILD_DIR) --output-on-failure

test-all: build
	cmake --build $(BUILD_DIR) --target test-all

test-property: build
	cmake --build $(BUILD_DIR) --target test-property

install: build
	cmake --build $(BUILD_DIR) --target install

package-macos:
	cmake -S . -B $(MACOS_PACKAGE_BUILD_DIR) -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES="$(MACOS_PACKAGE_ARCHS)" -DCMAKE_OSX_DEPLOYMENT_TARGET=$(MACOS_DEPLOYMENT_TARGET)
	cmake --build $(MACOS_PACKAGE_BUILD_DIR)
	cmake -DBINARY=$(MACOS_PACKAGE_BUILD_DIR)/breadlang -DDIST_DIR=$(MACOS_PACKAGE_DIST_DIR) -P scripts/package_macos.cmake

package-macos-universal:
	$(MAKE) package-macos MACOS_PACKAGE_ARCHS="arm64;x86_64" MACOS_PACKAGE_BUILD_DIR=build-macos-universal MACOS_PACKAGE_DIST_DIR=dist/macos-universal
