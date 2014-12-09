# plugin tests
PLUGIN_TESTS = test-btrfs test-lvm test-loop test-swap

all: build

build:
	scons -Q build

install:
	scons -Q install

uninstall:
	scons -Q -c install

test_%:
	scons -Q build/$@

test-%: test_%
	@echo "***Running tests***"
	LD_LIBRARY_PATH=build/ build/$<

%.so:
	scons -Q build/$@

BlockDev-1.0.gir:
	scons -Q BlockDev-1.0.gir

BlockDev-1.0.typelib:
	scons -Q BlockDev-1.0.typelib

plugins-test: ${PLUGIN_TESTS}

test-from-python: all
	GI_TYPELIB_PATH=build LD_LIBRARY_PATH=build python -c 'from gi.repository import BlockDev; BlockDev.init(None, None); print BlockDev.lvm_get_max_lv_size()'

run-ipython: all
	GI_TYPELIB_PATH=build/ LD_LIBRARY_PATH=build ipython

run-root-ipython: all
	sudo GI_TYPELIB_PATH=build/ LD_LIBRARY_PATH=build ipython

test: all
	@echo
	@sudo GI_TYPELIB_PATH=build LD_LIBRARY_PATH=build PYTHONPATH=.:tests/ \
		python -m unittest discover -v -s tests/ -p '*_test.py'

fast-test: all
	@echo
	@sudo SKIP_SLOW= GI_TYPELIB_PATH=build LD_LIBRARY_PATH=build PYTHONPATH=.:tests/ \
		python -m unittest discover -v -s tests/ -p '*_test.py'

clean:
	-rm -rf build/
	-rm src/lib/test_library
	-rm src/plugins/test_loop
	-rm src/plugins/test_lvm
	-rm src/plugins/test_swap
	-rm src/utils/test_sizes
	find . -name '*.pyc' -exec rm -f {} \;
	find . -name '*.pyo' -exec rm -f {} \;
