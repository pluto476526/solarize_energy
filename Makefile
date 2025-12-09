# ============================================================================
# Energy Management System Makefile
# ============================================================================
# Build system for Residential Solar and Agricultural Energy Management System
# Version: 1.0.0
# ============================================================================

# ============================================================================
# BUILD CONFIGURATION
# ============================================================================

# Compiler and toolchain
CC := gcc
AR := ar
RANLIB := ranlib
STRIP := strip
INSTALL := install
MKDIR := mkdir -p
RM := rm -f
RMDIR := rm -rf

# Project information
PROJECT_NAME := solarize
PROJECT_VERSION := 1.0.0
TARGET := $(PROJECT_NAME)

# Installation directories
PREFIX := /usr/local
BINDIR := $(PREFIX)/bin
SBINDIR := $(PREFIX)/sbin
LIBDIR := $(PREFIX)/lib
INCLUDEDIR := $(PREFIX)/include
DATADIR := $(PREFIX)/share
SYSCONFDIR := /etc
LOCALSTATEDIR := /var
SYSTEMDDIR := /lib/systemd/system

# Build directories
BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj
BIN_DIR := $(BUILD_DIR)/bin
LIB_DIR := $(BUILD_DIR)/lib
DEP_DIR := $(BUILD_DIR)/dep

# Include paths
INCLUDE_DIRS := -I. -Iinclude -Iconfig

# compilation flags
STRICT_CFLAGS := \
	-std=c17 \
	-D_GNU_SOURCE \
	-Wall \
	-Wextra \
	-Werror \
	-Wshadow \
	-Wpointer-arith \
	-Wcast-qual \
	-Wstrict-prototypes \
	-Wmissing-prototypes \
	-Wmissing-declarations \
	-Wunused \
	-Wuninitialized \
	-Winit-self \
	-Wformat=2 \
	-Wswitch-enum \
	-Wnull-dereference \
	-Wlogical-op \
	-fstack-protector-strong \
	-fPIE \
	-fno-common \
	$(INCLUDE_DIRS)

# Debug flags
DEBUG_CFLAGS := \
	-O0 \
	-g3 \
	-ggdb \
	-DDEBUG=1 \
	-fsanitize=address \
	-fsanitize=undefined \
	-fno-omit-frame-pointer

# Release flags
RELEASE_CFLAGS := \
	-O2 \
	-DNDEBUG=1 \
	-ftree-vectorize \
	-finline-functions

# Security hardening flags
SECURITY_CFLAGS := \
	-D_POSIX_C_SOURCE=200809L \
	-D_XOPEN_SOURCE=700 \
	-D_FORTIFY_SOURCE=0 \
	-fstack-clash-protection \
	-Wformat-security

# Linker flags
LDFLAGS := \
	-Wl,-z,relro \
	-Wl,-z,now \
	-Wl,-z,noexecstack \
	-Wl,--as-needed \
	-Wl,--no-undefined \
	-Wl,--gc-sections

# External libraries
EXTERNAL_LIBS := \
	-lm \
	-lpthread \
	-lssl \
	-lcrypto \
	-ljansson

# Core system sources
CORE_SRCS := \
	src/main.c \
	src/config.c \
	src/pv.c \
	src/battery.c \
	src/loads.c \
	src/agriculture.c \
	src/ev.c \
	src/controller.c \
    src/logging.c

# HAL sources
#HAL_SRCS := \
	src/hal.c \
	src/hal_setup.c \
	src/hal_integration.c

# Web server sources
# WEB_SRCS := \
    src/webserver.c \
    src/api_handler.c

# All source files
SRCS := $(CORE_SRCS) $(HAL_SRCS) $(WEB_SRCS)

# Header files
HEADERS := \
	include/core.h \
    include/logging.h \
	include/config.h \
	include/pv.h \
	include/battery.h \
	include/loads.h \
	include/agriculture.h \
	include/ev.h \
	include/controller.h \

# Object files
OBJS := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(SRCS))

# Dependency files
DEPS := $(patsubst src/%.c,$(DEP_DIR)/%.d,$(SRCS))

# Default build (release)
all: release
dev: debug

# Release build
release: CFLAGS := $(STRICT_CFLAGS) $(RELEASE_CFLAGS) $(SECURITY_CFLAGS)
release: LDFLAGS += -pie
release: $(BIN_DIR)/$(TARGET)
	@$(MAKE) stats

# Debug build
debug: CFLAGS := $(STRICT_CFLAGS) $(DEBUG_CFLAGS) $(SECURITY_CFLAGS)
debug: LDFLAGS += -pie
debug: $(BIN_DIR)/$(TARGET)-debug
	@$(MAKE) stats-debug

# Production build (stripped, no debug symbols)
production: CFLAGS := $(STRICT_CFLAGS) $(RELEASE_CFLAGS) $(SECURITY_CFLAGS)
production: LDFLAGS += -pie -s
production: $(BIN_DIR)/$(TARGET)-prod
	@$(MAKE) stats-prod

# Static library
static: CFLAGS := $(STRICT_CFLAGS) $(RELEASE_CFLAGS) $(SECURITY_CFLAGS)
static: $(LIB_DIR)/lib$(PROJECT_NAME).a
	@$(MAKE) stats-static

# Link executable
$(BIN_DIR)/$(TARGET): $(OBJS)
	@echo "  LINK    $@"
	@$(MKDIR) $(BIN_DIR)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS) $(EXTERNAL_LIBS)

$(BIN_DIR)/$(TARGET)-debug: $(OBJS)
	@echo "  LINK    $@"
	@$(MKDIR) $(BIN_DIR)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS) $(EXTERNAL_LIBS)

$(BIN_DIR)/$(TARGET)-prod: $(OBJS)
	@echo "  LINK    $@"
	@$(MKDIR) $(BIN_DIR)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS) $(EXTERNAL_LIBS)
	$(STRIP) --strip-all --remove-section=.comment --remove-section=.note $@

# Build static library
$(LIB_DIR)/lib$(PROJECT_NAME).a: $(OBJS)
	@echo "  AR      $@"
	@$(MKDIR) $(LIB_DIR)
	$(AR) rcs $@ $(OBJS)
	$(RANLIB) $@

# Compile source files
$(OBJ_DIR)/%.o: src/%.c
	@echo "  CC      $<"
	@$(MKDIR) $(dir $@)
	@$(MKDIR) $(dir $(patsubst src/%.c,$(DEP_DIR)/%.d,$<))
	$(CC) $(CFLAGS) -MMD -MP -MF $(patsubst src/%.c,$(DEP_DIR)/%.d,$<) -c $< -o $@

# Include dependency files
-include $(DEPS)

# ============================================================================
# STATISTICS TARGETS
# ============================================================================

# Display build statistics
stats:
	@echo "\n==================== BUILD STATISTICS ===================="
	@echo "Project: $(PROJECT_NAME) v$(PROJECT_VERSION)"
	@echo "Build mode: Release"
	@echo "Build time: $$(date '+%Y-%m-%d %H:%M:%S')"
	@echo "Compiler: $(CC) $$($(CC) -dumpversion)"
	@echo "Target: $(TARGET)"
	@echo "-----------------------------------------------------------"
	@echo "Source files: $$(echo $(SRCS) | wc -w)"
	@echo "Header files: $$(echo $(HEADERS) | wc -w)"
	@echo "Total lines of code (approx):"
	@cat $(SRCS) $(HEADERS) 2>/dev/null | wc -l | awk '{print $$1}'
	@echo "Binary size:"
	@if [ -f $(BIN_DIR)/$(TARGET) ]; then \
		size=$(shell stat -c%s $(BIN_DIR)/$(TARGET) 2>/dev/null || echo "0"); \
		if [ $$size -gt 1048576 ]; then \
			echo "  $$(echo "scale=2; $$size / 1048576" | bc) MB"; \
		elif [ $$size -gt 1024 ]; then \
			echo "  $$(echo "scale=2; $$size / 1024" | bc) KB"; \
		else \
			echo "  $$size bytes"; \
		fi; \
		ls -lh $(BIN_DIR)/$(TARGET) | awk '{print "  " $$5 " (" $$9 ")"}'; \
	fi
	@echo "Memory usage breakdown:"
	@if [ -f $(BIN_DIR)/$(TARGET) ]; then \
		size $(BIN_DIR)/$(TARGET) 2>/dev/null || echo "    size command not available"; \
	fi
	@echo "Build path: $(BIN_DIR)/$(TARGET)"
	@echo "===========================================================\n"

stats-debug:
	@echo "\n==================== BUILD STATISTICS ===================="
	@echo "Project: $(PROJECT_NAME) v$(PROJECT_VERSION)"
	@echo "Build mode: Debug"
	@echo "Build time: $$(date '+%Y-%m-%d %H:%M:%S')"
	@echo "Compiler: $(CC) $$($(CC) -dumpversion)"
	@echo "Build path: $(BIN_DIR)/$(TARGET)-debug"
	@echo "===========================================================\n"

stats-prod:
	@echo "\n==================== BUILD STATISTICS ===================="
	@echo "Project: $(PROJECT_NAME) v$(PROJECT_VERSION)"
	@echo "Build mode: Production"
	@echo "Build time: $$(date '+%Y-%m-%d %H:%M:%S')"
	@echo "Compiler: $(CC) $$($(CC) -dumpversion)"
	@echo "Build path: $(BIN_DIR)/$(TARGET)-prod"
	@echo "===========================================================\n"

stats-static:
	@echo "\n==================== BUILD STATISTICS ===================="
	@echo "Project: $(PROJECT_NAME) v$(PROJECT_VERSION)"
	@echo "Build mode: Static Library"
	@echo "Build time: $$(date '+%Y-%m-%d %H:%M:%S')"
	@echo "Library: lib$(PROJECT_NAME).a"
	@echo "Object files in library: $$(ar t $(LIB_DIR)/lib$(PROJECT_NAME).a 2>/dev/null | wc -l || echo "0")"
	@echo "Build path: $(LIB_DIR)/lib$(PROJECT_NAME).a"
	@echo "===========================================================\n"

# ============================================================================
# STATIC ANALYSIS TARGETS
# ============================================================================

# Run cppcheck analysis
cppcheck:
	@echo "Running cppcheck analysis..."
	cppcheck --enable=all --suppress=missingIncludeSystem \
	         --suppress=unusedFunction --inconclusive \
	         --check-level=exhaustive --std=c11 $(SRCS) include/

# Run flawfinder security scan
flawfinder:
	@echo "Running flawfinder security scan..."
	flawfinder --quiet --context src/

# Run all static analysis tools
analyze: cppcheck flawfinder

# ============================================================================
# TESTING TARGETS
# ============================================================================

# Memory leak tests
memcheck: $(BIN_DIR)/$(TARGET)-debug
	@echo "Running memory leak check..."
	valgrind --leak-check=full --show-leak-kinds=all \
	         --track-origins=yes --trace-children=yes \
	         --error-exitcode=1 $(BIN_DIR)/$(TARGET)-debug

# Run all tests
test: memcheck

# ============================================================================
# INSTALLATION TARGETS
# ============================================================================

# Install binary
install: production
	@echo "Installing $(PROJECT_NAME) to $(DESTDIR)$(SBINDIR)..."
	$(INSTALL) -d $(DESTDIR)$(SBINDIR)
	$(INSTALL) -d $(DESTDIR)$(SYSCONFDIR)/$(PROJECT_NAME)
	$(INSTALL) -d $(DESTDIR)$(LOCALSTATEDIR)/log/$(PROJECT_NAME)
	$(INSTALL) -d $(DESTDIR)$(LOCALSTATEDIR)/lib/$(PROJECT_NAME)
	$(INSTALL) -d $(DESTDIR)$(DATADIR)/$(PROJECT_NAME)/static
	$(INSTALL) -m 0755 $(BIN_DIR)/$(TARGET)-prod $(DESTDIR)$(SBINDIR)/$(TARGET)
	$(INSTALL) -m 0644 static/* $(DESTDIR)$(DATADIR)/$(PROJECT_NAME)/static/
	$(INSTALL) -m 0644 config/* $(DESTDIR)$(SYSCONFDIR)/$(PROJECT_NAME)/ 2>/dev/null || true
	$(INSTALL) -m 0644 web.service $(DESTDIR)$(SYSTEMDDIR)/$(PROJECT_NAME).service
	@echo "Installation complete. Configuration files in $(SYSCONFDIR)/$(PROJECT_NAME)/"
	@echo "Web interface files in $(DATADIR)/$(PROJECT_NAME)/static/"
	@echo "Service file: $(SYSTEMDDIR)/$(PROJECT_NAME).service"

# Uninstall
uninstall:
	@echo "Uninstalling $(PROJECT_NAME)..."
	$(RM) $(DESTDIR)$(SBINDIR)/$(TARGET)
	$(RM) $(DESTDIR)$(SYSTEMDDIR)/$(PROJECT_NAME).service
	$(RMDIR) $(DESTDIR)$(SYSCONFDIR)/$(PROJECT_NAME)
	$(RMDIR) $(DESTDIR)$(LOCALSTATEDIR)/log/$(PROJECT_NAME)
	$(RMDIR) $(DESTDIR)$(LOCALSTATEDIR)/lib/$(PROJECT_NAME)
	$(RMDIR) $(DESTDIR)$(DATADIR)/$(PROJECT_NAME)
	@echo "Uninstall complete"

# ============================================================================
# DEVELOPMENT TARGETS
# ============================================================================

# Format source code
format:
	@echo "Formatting source code..."
	clang-format -i $(SRCS) $(HEADERS)
	@echo "Formatting complete"

# Code metrics
metrics:
	@echo "\n==================== CODE METRICS ===================="
	@echo "Lines of code:"
	@echo "  Source files:"
	@wc -l $(SRCS) 2>/dev/null | tail -1 | awk '{print "    Total: " $$1}'
	@echo "  Header files:"
	@wc -l $(HEADERS) 2>/dev/null | tail -1 | awk '{print "    Total: " $$1}'
	@echo "  All files:"
	@cat $(SRCS) $(HEADERS) 2>/dev/null | wc -l | awk '{print "    Total: " $$1}'
	@echo ""
	@echo "File count:"
	@echo "  Source files: $$(echo $(SRCS) | wc -w)"
	@echo "  Header files: $$(echo $(HEADERS) | wc -w)"
	@echo ""
	@echo "Function count (approx):"
	@grep -E '^\w+\s+\w+\([^)]*\)\s*\{' $(SRCS) 2>/dev/null | wc -l | awk '{print "  " $$1 " functions"}'
	@echo "=====================================================\n"

# Clean build artifacts
clean:
	@echo "Cleaning build artifacts..."
	$(RM) -r $(BUILD_DIR)
	@echo "Clean complete"

# Distclean (remove everything)
distclean: clean
	@echo "Cleaning distribution..."
	$(RM) *.o *.a *.so *.tar.gz
	$(RM) compile_commands.json
	@echo "Distclean complete"

# ============================================================================
# HELP TARGET
# ============================================================================

# Display help information
help:
	@echo "Energy Management System Build System"
	@echo "======================================"
	@echo ""
	@echo "Available targets:"
	@echo ""
	@echo "BUILD TARGETS:"
	@echo "  all               Build release version (default)"
	@echo "  release           Build optimized release version"
	@echo "  debug             Build with debug symbols and sanitizers"
	@echo "  production        Build stripped production version"
	@echo "  static            Build static library"
	@echo ""
	@echo "STATISTICS:"
	@echo "  stats             Show release build statistics"
	@echo "  stats-debug       Show debug build statistics"
	@echo "  stats-prod        Show production build statistics"
	@echo "  stats-static      Show static library statistics"
	@echo ""
	@echo "STATIC ANALYSIS:"
	@echo "  cppcheck          Run cppcheck analysis"
	@echo "  flawfinder        Run flawfinder security scan"
	@echo "  analyze           Run all static analysis tools"
	@echo ""
	@echo "TESTING:"
	@echo "  memcheck          Run memory leak checks with Valgrind"
	@echo "  test              Run all tests"
	@echo ""
	@echo "INSTALLATION:"
	@echo "  install           Install binary and configuration files"
	@echo "  uninstall         Remove installed files"
	@echo ""
	@echo "DEVELOPMENT:"
	@echo "  format            Format source code with clang-format"
	@echo "  metrics           Display code metrics"
	@echo "  clean             Remove build artifacts"
	@echo "  distclean         Remove all generated files"
	@echo ""
	@echo "HELP:"
	@echo "  help              Display this help message"
	@echo ""
	@echo "ENVIRONMENT VARIABLES:"
	@echo "  PREFIX            Installation prefix (default: /usr/local)"
	@echo "  DESTDIR           Destination directory for staged installs"
	@echo "  CC                C compiler (default: gcc)"
	@echo ""
	@echo "EXAMPLES:"
	@echo "  make debug CC=clang           Build debug version with Clang"
	@echo "  make install DESTDIR=/tmp/pkg Install to temporary directory"
	@echo "  make analyze                  Run all static analysis tools"
	@echo "  make test                     Run all tests"
	@echo ""

# ============================================================================
# PHONY TARGET DECLARATIONS
# ============================================================================

.PHONY: all release debug production static \
        stats stats-debug stats-prod stats-static \
        cppcheck flawfinder analyze \
        memcheck test \
        install uninstall \
        format metrics clean distclean help
