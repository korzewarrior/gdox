VERSION := $(shell python scripts/project_version.py)
LINUX_TARGET := x86_64-unknown-linux-gnu

.PHONY: all configure build test check shellcheck privacy-check android-debug android-source release-linux release-steamdeck source

all: build

configure:
	cmake --preset dev

build: configure
	cmake --build --preset dev --parallel

test: build
	ctest --preset dev --output-on-failure

shellcheck:
	git grep --untracked -Ilz -E '^#!.*(sh|bash)([[:space:]]|$$)' -- . | \
		xargs -0 shellcheck -x

check: shellcheck
	cmake --preset core
	cmake --build --preset core --parallel
	ctest --preset core --label-exclude host-neutral --output-on-failure
	cmake --preset dev
	cmake --build --preset dev --parallel
	ctest --preset dev --output-on-failure
	PYTHONPYCACHEPREFIX=../gdox-output/cache/python python -m py_compile scripts/*.py
	bash -n packaging/install-device-access.sh packaging/linux/xenia-native \
		packaging/linux/xenia-proton
	sh -n packaging/linux/gdox packaging/linux/install.sh \
		packaging/linux/nbdfuse packaging/linux/xemu scripts/build_android.sh \
		scripts/prepare_android_emulator.sh scripts/prepare_android_sdl2.sh
	python -m json.tool packaging/runtime-manifest.json >/dev/null
	python -m json.tool packaging/xenia-compatibility.json >/dev/null
	command -v zstd >/dev/null
	python scripts/fetch_runtime.py validate
	python scripts/xenia_compatibility.py
	python scripts/generate_xenia_policy.py --check
	python scripts/audit_architecture.py
	python scripts/audit_xbox_release.py
	python scripts/audit_release.py --path .
	python scripts/audit_version.py
	git diff --check

privacy-check:
	python scripts/audit_release.py

android-debug:
	scripts/build_android.sh debug

android-source:
	python scripts/package_android_source.py --version $(VERSION)

release-linux:
	python scripts/build_linux_packages.py --version $(VERSION) --linux-only

release-steamdeck:
	python scripts/build_linux_packages.py --version $(VERSION)

source:
	python scripts/package_corresponding_source.py --version $(VERSION)
