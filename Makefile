# includes
GLIB_INCLUDES := `pkg-config --cflags glib-2.0`

# libraries
GLIB := `pkg-config --libs glib-2.0`
GOBJECT := `pkg-config --libs gobject-2.0`

# library for internal use
UTILS_SOURCE_FILES := src/utils/exec.c src/utils/sizes.c
UTILS_OBJS := $(patsubst %.c,%.o,${UTILS_SOURCE_FILES})
UTILS_HEADER_FILES := $(patsubst %.c,%.h,${UTILS_SOURCE_FILES})

# stub functions for plugins
PLUGIN_HEADER_NAMES = crypto.h dm.h loop.h lvm.h mpath.h swap.h btrfs.h mdraid.h
PLUGIN_HEADER_FILES := $(addprefix src/lib/plugin_apis/, ${PLUGIN_HEADER_NAMES})
PLUGIN_SOURCE_FILES := $(patsubst %.h,%.c,${PLUGIN_HEADER_FILES})

# implemented plugins
PLUGIN_SOURCES := src/plugins/crypto.c src/plugins/dm.c src/plugins/loop.c src/plugins/lvm.c src/plugins/mpath.c src/plugins/swap.c \
                  src/plugins/btrfs.c src/plugins/mdraid.c
PLUGIN_OBJS := $(patsubst %.c,%.o,${PLUGIN_SOURCES})
PLUGIN_LIBS := $(addprefix src/plugins/,$(patsubst %.c,libbd_%.so,$(notdir ${PLUGIN_SOURCES})))

# plugin tests
PLUGIN_TEST_SOURCES := $(wildcard src/plugins/test_*.c)
PLUGIN_TEST_EXECUTABLES := $(addprefix src/plugins/,$(patsubst test_%.c,test_%,$(notdir ${PLUGIN_TEST_SOURCES})))
PLUGIN_TESTS := $(patsubst test_%.c,test-%,$(notdir ${PLUGIN_TEST_SOURCES}))

all:
	scons -Q build

# test object files include the source of the programs they test
test_%.o: test_%.c %.c %.h
	gcc -c -Wall -Wextra -Werror -o $@ -I src/utils/ ${GLIB_INCLUDES} $<

test_%: test_%.o ${UTILS_OBJS}
	gcc -o $@ ${GLIB} $^

test-%: src/plugins/test_%
	@echo "***Running tests***"
	./$<

# test_sizes executable must avoid two copies of sizes.o
src/utils/test_sizes: src/utils/test_sizes.o
	gcc -o $@ ${GLIB} $<

test-sizes: src/utils/test_sizes
	@echo "***Running tests***"
	./$<

# compilation does not signal all warnings, as it includes stub sources
src/lib/test_blockdev.o: src/lib/test_blockdev.c src/lib/blockdev.c src/lib/blockdev.h ${PLUGIN_SOURCE_FILES}
	gcc -c -Wextra -Werror -o $@ -I src/utils/ -I src/plugins ${GLIB_INCLUDES} $<

src/lib/test_library: src/lib/test_blockdev.o ${PLUGIN_LIBS} src/utils/libbd_utils.so
	gcc -o $@ -L src/utils/ -lbd_utils -ldl ${GLIB} ${GOBJECT} $<

test-library: src/lib/test_library
	@echo "***Running tests***"
	LD_LIBRARY_PATH=src/plugins/:src/utils ./$<

test-plugins: ${PLUGIN_TESTS}

%.so:
	scons -Q build/$@

BlockDev-1.0.gir:
	scons -Q BlockDev-1.0.gir

BlockDev-1.0.typelib:
	scons -Q BlockDev-1.0.typelib

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
