VERSION := $(shell sed -n 's/^[[:space:]]*VERSION \([0-9][0-9.]*\).*/\1/p' CMakeLists.txt | head -n1)
LINUX_TARGET := x86_64-unknown-linux-gnu

.PHONY: all configure build test check site site-check privacy-check android-debug android-source release-linux release-steamdeck source

all: build

configure:
	cmake --preset dev

build: configure
	cmake --build --preset dev --parallel

test: build
	ctest --preset dev --output-on-failure

check: site-check
	cmake --preset core
	cmake --build --preset core --parallel
	ctest --preset core --output-on-failure
	cmake --preset dev
	cmake --build --preset dev --parallel
	ctest --preset dev --output-on-failure
	PYTHONPYCACHEPREFIX=../gdox-output/cache/python python -m py_compile scripts/*.py
	bash -n packaging/install-device-access.sh
	sh -n packaging/linux/gdox packaging/linux/install.sh
	python -m json.tool packaging/runtime-manifest.json >/dev/null
	python scripts/audit_architecture.py
	python scripts/audit_release.py
	git diff --check

site:
	python scripts/build_site.py

site-check: site
	python scripts/audit_site.py

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
	python scripts/package_source.py --version $(VERSION)
