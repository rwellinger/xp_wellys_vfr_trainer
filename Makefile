SHELL := /bin/bash

XPLANE_ROOT := /Users/robertw/X-Plane 12
PLUGIN_DIR  := $(XPLANE_ROOT)/Resources/available plugins/xp_wellys_vfr_trainer

SDK_SENTINEL    := sdk/XPLM/XPLMPlugin.h
IMGUI_SENTINEL  := vendor/imgui/imgui.h
JSON_SENTINEL   := vendor/json.hpp
CATCH2_SENTINEL := vendor/catch2/catch_amalgamated.hpp

CATCH2_VERSION := 3.15.1

.PHONY: all help setup build install test test-unit lint format clean distclean \
        ci-remote win-artifact release release-build \
        cleanup-tags cleanup-branches cleanup-runs

.DEFAULT_GOAL := help

all: clean format build lint test

# ── Help ──────────────────────────────────────────────────────────────────────
help:
	@echo "xp_wellys_vfr_trainer - Makefile targets"
	@echo ""
	@echo "  make                Show this help (default)"
	@echo "  make setup          Download X-Plane SDK, Dear ImGui, nlohmann/json, Catch2"
	@echo "  make build          Build universal plugin (arm64 + x86_64) -> build/xp_wellys_vfr_trainer.xpl"
	@echo "  make test           Build + run Catch2 unit tests"
	@echo "  make install        Install + ad-hoc codesign the plugin into X-Plane"
	@echo "  make format         Run clang-format on src/*.cpp src/*.hpp"
	@echo "  make lint           Run clang-tidy on src/*.cpp"
	@echo "  make clean          Remove build dirs"
	@echo "  make distclean      clean + remove sdk/ and vendor/"
	@echo ""
	@echo "  make ci-remote      Trigger the GitHub Actions build (mac + Windows)"
	@echo "  make win-artifact   Download the latest Windows CI .xpl into dist-win/"
	@echo ""
	@echo "  make release VERSION=x.y.z  Bump VERSION.txt, commit, tag v<ver> + push (triggers CI release)"
	@echo "  make release-build          Local universal build with the version from VERSION.txt"
	@echo ""
	@echo "  make cleanup-tags      Prune local tags no longer on origin"
	@echo "  make cleanup-branches  Prune local branches whose remote is gone"
	@echo "  make cleanup-runs      Delete all GitHub Actions runs except the newest per workflow"

# ── Setup ─────────────────────────────────────────────────────────────────────
setup: $(SDK_SENTINEL) $(IMGUI_SENTINEL) $(JSON_SENTINEL) $(CATCH2_SENTINEL)
	@echo "Setup complete. Run 'make build' to compile."

$(SDK_SENTINEL):
	@echo "Downloading X-Plane SDK..."
	@set -euo pipefail; \
	TMP=$$(mktemp -d); \
	trap "rm -rf $$TMP" EXIT; \
	curl -fsSL "https://developer.x-plane.com/wp-content/plugins/code-sample-generation/sdk_zip_files/XPSDK430.zip" \
	     -o "$$TMP/sdk.zip"; \
	unzip -q "$$TMP/sdk.zip" -d "$$TMP/sdk_extracted"; \
	mkdir -p sdk/XPLM sdk/XPWidgets sdk/Libraries/Mac sdk/Libraries/Win; \
	find "$$TMP/sdk_extracted" -path "*/CHeaders/XPLM/*.h"    -exec cp {} sdk/XPLM/ \;; \
	find "$$TMP/sdk_extracted" -path "*/CHeaders/Widgets/*.h" -exec cp {} sdk/XPWidgets/ \;; \
	cp -R "$$TMP/sdk_extracted"/*/Libraries/Mac/*.framework sdk/Libraries/Mac/ 2>/dev/null || \
	find "$$TMP/sdk_extracted" -name "*.framework" -exec cp -R {} sdk/Libraries/Mac/ \;; \
	find "$$TMP/sdk_extracted" -path "*/Libraries/Win/*.lib" -exec cp {} sdk/Libraries/Win/ \; 2>/dev/null || true
	@echo "SDK headers installed (Mac frameworks + Win link libs)."

$(IMGUI_SENTINEL):
	@echo "Downloading Dear ImGui v1.92.8..."
	@set -euo pipefail; \
	TMP=$$(mktemp -d); \
	trap "rm -rf $$TMP" EXIT; \
	mkdir -p vendor/imgui/backends; \
	curl -fsSL "https://github.com/ocornut/imgui/archive/refs/tags/v1.92.8.zip" -o "$$TMP/imgui.zip"; \
	unzip -q "$$TMP/imgui.zip" -d "$$TMP/"; \
	SRC="$$TMP/imgui-1.92.8"; \
	cp "$$SRC"/imgui.{h,cpp} vendor/imgui/; \
	cp "$$SRC"/imgui_{draw,tables,widgets}.cpp vendor/imgui/; \
	cp "$$SRC"/imgui_internal.h "$$SRC"/imconfig.h vendor/imgui/; \
	cp "$$SRC"/imstb_textedit.h "$$SRC"/imstb_rectpack.h "$$SRC"/imstb_truetype.h vendor/imgui/ 2>/dev/null || true; \
	cp "$$SRC"/backends/imgui_impl_opengl2.{h,cpp} vendor/imgui/backends/
	@echo "Dear ImGui installed."

$(JSON_SENTINEL):
	@echo "Downloading nlohmann/json v3.12.0..."
	@mkdir -p vendor
	@curl -fsSL "https://github.com/nlohmann/json/releases/download/v3.12.0/json.hpp" \
	     -o vendor/json.hpp
	@echo "nlohmann/json installed."

$(CATCH2_SENTINEL):
	@echo "Downloading Catch2 v$(CATCH2_VERSION) (amalgamated)..."
	@set -euo pipefail; \
	TMP=$$(mktemp -d); \
	trap "rm -rf $$TMP" EXIT; \
	mkdir -p vendor/catch2; \
	curl -fsSL "https://github.com/catchorg/Catch2/archive/refs/tags/v$(CATCH2_VERSION).tar.gz" \
	     -o "$$TMP/catch2.tar.gz"; \
	tar -xzf "$$TMP/catch2.tar.gz" -C "$$TMP/"; \
	cp "$$TMP/Catch2-$(CATCH2_VERSION)/extras/catch_amalgamated.hpp" vendor/catch2/; \
	cp "$$TMP/Catch2-$(CATCH2_VERSION)/extras/catch_amalgamated.cpp" vendor/catch2/
	@echo "Catch2 installed."

# ── Build ─────────────────────────────────────────────────────────────────────
# Single universal build: the trainer is cloud-only (no local inference), so
# both arm64 and x86_64 slices share an identical configuration. One CMake
# build with CMAKE_OSX_ARCHITECTURES="arm64;x86_64" produces a universal .xpl
# directly — no per-arch build dirs, no lipo merge.
build: $(SDK_SENTINEL) $(IMGUI_SENTINEL) $(JSON_SENTINEL) $(CATCH2_SENTINEL)
	@echo "=== Building universal xp_wellys_vfr_trainer (arm64 + x86_64) ==="
	cmake -B build -DCMAKE_BUILD_TYPE=Release \
	    -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" \
	    -DBUILD_TESTS=OFF \
	    -Wno-dev
	cmake --build build --parallel
	@echo ""
	@file build/xp_wellys_vfr_trainer.xpl
	@lipo -info build/xp_wellys_vfr_trainer.xpl
	@echo "Done. Run 'make install' to deploy the universal .xpl."

# ── Tests ─────────────────────────────────────────────────────────────────────
# Host-arch only (no universal needed for a dev tool). Separate build dir so it
# never collides with the universal `build/` Release tree.
test: test-unit
test-unit: $(SDK_SENTINEL) $(IMGUI_SENTINEL) $(JSON_SENTINEL) $(CATCH2_SENTINEL)
	@echo "=== Building unit tests ==="
	cmake -B build-test -DCMAKE_BUILD_TYPE=Release \
	    -DCMAKE_OSX_ARCHITECTURES="$(shell uname -m)" \
	    -DBUILD_TESTS=ON \
	    -Wno-dev
	cmake --build build-test --target xp_wellys_vfr_trainer_tests --parallel
	@echo ""
	@echo "=== Running unit tests ==="
	@./build-test/xp_wellys_vfr_trainer_tests --order rand --rng-seed 42

# ── Install ───────────────────────────────────────────────────────────────────
install:
	@if [ ! -f "build/xp_wellys_vfr_trainer.xpl" ]; then \
	    echo "Plugin not built yet. Run 'make build' first."; exit 1; \
	fi
	@echo "=== Installing xp_wellys_vfr_trainer ==="
	@mkdir -p "$(PLUGIN_DIR)/mac_x64"
	@cp build/xp_wellys_vfr_trainer.xpl "$(PLUGIN_DIR)/mac_x64/"
	@xattr -dr com.apple.quarantine "$(PLUGIN_DIR)/mac_x64/xp_wellys_vfr_trainer.xpl" 2>/dev/null || true
	@codesign --force --deep --sign - "$(PLUGIN_DIR)/mac_x64/xp_wellys_vfr_trainer.xpl"
	@mkdir -p "$(PLUGIN_DIR)/data"
	@if [ ! -f "$(PLUGIN_DIR)/data/settings.json" ]; then \
	    cp data/settings.json "$(PLUGIN_DIR)/data/"; \
	    echo "Installed: $(PLUGIN_DIR)/data/settings.json"; \
	else \
	    echo "Kept existing settings.json"; \
	fi
	@echo "Installed and signed."

# ── Lint / Format ─────────────────────────────────────────────────────────────
format:
	@command -v clang-format >/dev/null 2>&1 || { \
	    echo "clang-format not found. Install with: brew install llvm"; exit 1; }
	clang-format -i src/main.cpp src/*/*.cpp src/*/*.hpp

lint: $(SDK_SENTINEL) $(IMGUI_SENTINEL) $(JSON_SENTINEL) $(CATCH2_SENTINEL)
	@command -v clang-tidy >/dev/null 2>&1 || { \
	    echo "clang-tidy not found. Install with: brew install llvm"; exit 1; }
	cmake -B build-lint -DCMAKE_BUILD_TYPE=Release -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
	    -DCMAKE_OSX_ARCHITECTURES="$(shell uname -m)" -Wno-dev
	@# Exclude *_win.cpp: they pull <windows.h>/<wincred.h>, unresolvable on
	@# the macOS toolchain.
	clang-tidy -p build-lint --extra-arg="-isysroot" --extra-arg="$(shell xcrun --show-sdk-path)" \
	    $(shell find src -name '*.cpp' ! -name '*_win.cpp')

# ── Release ─────────────────────────────────────────────────────────────────────
# Cut a release: bump VERSION.txt, commit, tag v<VERSION> and push. The tag push
# triggers the CI `release` job (.github/workflows/build.yml), which builds both
# slices, folds them into the drop-in ZIP (mac_x64/ + win_x64/), generates the
# SkunkCrafts control files and publishes the GitHub release + `release` branch.
release:
	@if [ -z "$(VERSION)" ]; then \
	    echo "Usage: make release VERSION=1.2.1"; exit 1; \
	fi
	@if ! git diff --quiet || ! git diff --cached --quiet; then \
	    echo "Uncommitted changes present. Commit or stash first."; exit 1; \
	fi
	@if [ -n "$$(git ls-files --others --exclude-standard)" ]; then \
	    echo "Untracked files present. Commit or clean up first."; exit 1; \
	fi
	@echo "$(VERSION)" > VERSION.txt
	@git add VERSION.txt
	@git commit -m "release $(VERSION)"
	@git push origin main
	@git tag -a "v$(VERSION)" -m "Release $(VERSION)"
	@git push origin "v$(VERSION)"
	@echo "Released v$(VERSION) and pushed tag to origin."

# Local universal release build. The trainer bakes the version straight from
# VERSION.txt (no separate -DRELEASE flag), so this is just `make build` — kept
# as a named target to mirror the ATC workflow and document intent.
release-build: build
	@echo "Done. Universal release build with version from VERSION.txt."

# ── CI (Windows via GitHub Actions) ────────────────────────────────────────────
# GitHub Actions is the Windows compiler — there is no local MSVC toolchain.
# `ci-remote` builds the PUSHED state of the current branch, not the working tree.
BRANCH := $(shell git rev-parse --abbrev-ref HEAD)

ci-remote:
	@command -v gh >/dev/null 2>&1 || { echo "gh CLI not found. Install with: brew install gh"; exit 1; }
	@if [ -n "$$(git status --porcelain)" ]; then \
	    echo "WARNING: uncommitted changes — CI builds the pushed state of '$(BRANCH)', not your worktree."; fi
	@if [ -n "$$(git log origin/$(BRANCH)..$(BRANCH) 2>/dev/null)" ]; then \
	    echo "WARNING: unpushed commits on '$(BRANCH)' — push first so CI sees them."; fi
	gh workflow run build.yml --ref "$(BRANCH)"
	@echo "Triggered. Watch: gh run watch  (or: gh run list --workflow=build.yml)"

win-artifact:
	@command -v gh >/dev/null 2>&1 || { echo "gh CLI not found. Install with: brew install gh"; exit 1; }
	@rm -rf dist-win
	gh run download -n xp_wellys_vfr_trainer-win -D dist-win
	@echo "Downloaded Windows artifact into dist-win/."

# ── Cleanup (git + CI housekeeping) ────────────────────────────────────────────
cleanup-tags:
	git fetch --prune --prune-tags origin
	@echo "Local tags synced with remote."

cleanup-branches:
	@echo "Pruning remote-tracking references..."
	@git fetch --prune origin
	@echo ""
	@echo "Local branches whose upstream is gone:"
	@STALE=$$(git for-each-ref --format '%(refname:short) %(upstream:track)' refs/heads | awk '$$2 == "[gone]" {print $$1}'); \
	if [ -z "$$STALE" ]; then \
	    echo "  (none)"; \
	else \
	    echo "$$STALE" | sed 's/^/  /'; \
	    echo ""; \
	    echo "$$STALE" | xargs -n1 git branch -d; \
	fi
	@echo "Local branches synced with remote."

cleanup-runs:
	@command -v gh >/dev/null 2>&1 || { \
	    echo "gh not found. Install with: brew install gh"; exit 1; }
	@echo "Deleting GitHub Actions runs (keeping newest per workflow)..."
	@for wf in $$(gh workflow list --json id -q '.[].id'); do \
	    gh run list --workflow=$$wf --limit 1000 --json databaseId -q '.[1:] | .[].databaseId' \
	        | xargs -I {} gh run delete {}; \
	done
	@echo "Cleanup complete."

# ── Clean ─────────────────────────────────────────────────────────────────────
clean:
	rm -rf build/ build-test/ build-lint/ build-win/ dist/ dist-win/

distclean: clean
	rm -rf sdk/ vendor/
	@echo "Removed sdk/ and vendor/. Run 'make setup' to re-download dependencies."
