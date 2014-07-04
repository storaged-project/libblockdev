# includes
GLIB_INCLUDES := `pkg-config --cflags glib-2.0`
GOBJECT_INCLUDES := `pkg-config --cflags gobject-2.0`
LIBCRYPTSETUP_INCLUDES := `pkg-config --cflags libcryptsetup`

# libraries
GLIB := `pkg-config --libs glib-2.0`
GOBJECT := `pkg-config --libs gobject-2.0`
LIBCRYPTSETUP := `pkg-config --libs libcryptsetup`

# library for internal use
UTILS_SOURCES := src/utils/exec.c src/utils/sizes.c
UTILS_OBJS := $(patsubst %.c,%.o,${UTILS_SOURCES})

# stub functions for plugins
PLUGIN_HEADER_NAMES = crypto.h dm.h loop.h lvm.h mpath.h swap.h btrfs.h
PLUGIN_HEADER_FILES := $(addprefix src/lib/plugin_apis/, ${PLUGIN_HEADER_NAMES})
PLUGIN_SOURCE_FILES := $(patsubst %.h,%.c,${PLUGIN_HEADER_FILES})

# implemented plugins
PLUGIN_SOURCES := src/plugins/crypto.c src/plugins/dm.c src/plugins/loop.c src/plugins/lvm.c src/plugins/mpath.c src/plugins/swap.c \
                  src/plugins/btrfs.c
PLUGIN_OBJS := $(patsubst %.c,%.o,${PLUGIN_SOURCES})
PLUGIN_LIBS := $(addprefix src/plugins/,$(patsubst %.c,libbd_%.so,$(notdir ${PLUGIN_SOURCES})))

# plugin tests
PLUGIN_TEST_SOURCES := $(wildcard src/plugins/test_*.c)
PLUGIN_TEST_EXECUTABLES := $(addprefix src/plugins/,$(patsubst test_%.c,test_%,$(notdir ${PLUGIN_TEST_SOURCES})))
PLUGIN_TESTS := $(patsubst test_%.c,test-%,$(notdir ${PLUGIN_TEST_SOURCES}))

# plugin management
LIBRARY_FILES := src/lib/blockdev.c src/lib/blockdev.h src/lib/plugins.h

all: BlockDev-1.0.typelib

# object files
%.o: %.c %.h
	gcc -c -Wall -Wextra -Werror -fPIC -o $@ -I src/utils/ ${GLIB_INCLUDES} $<

# test object files include the source of the programs they test
test_%.o: test_%.c %.c %.h
	gcc -c -Wall -Wextra -Werror -o $@ -I src/utils/ ${GLIB_INCLUDES} $<

test_%: test_%.o ${UTILS_OBJS}
	gcc -o $@ -lm ${GLIB} $^

test-%: src/plugins/test_%
	@echo "***Running tests***"
	./$<

# test_sizes executable must avoid two copies of sizes.o
src/utils/test_sizes: src/utils/test_sizes.o
	gcc -o $@ -lm ${GLIB} $<

test-sizes: src/utils/test_sizes
	@echo "***Running tests***"
	./$<

# compilation does not signal all warnings, as it includes stub sources
src/lib/test_blockdev.o: src/lib/test_blockdev.c src/lib/blockdev.c src/lib/blockdev.h ${PLUGIN_SOURCE_FILES}
	gcc -c -Wextra -Werror -o $@ -I src/utils/ -I src/plugins ${GLIB_INCLUDES} $<

src/lib/test_library: src/lib/test_blockdev.o ${PLUGIN_LIBS}
	gcc -o $@ -ldl ${GLIB} ${GOBJECT} $<

test-library: src/lib/test_library
	@echo "***Running tests***"
	LD_LIBRARY_PATH=src/plugins/ ./$<

test-plugins: ${PLUGIN_TESTS}

# individual plugin libraries
libbd_%.so: %.o
	gcc -shared -fPIC -o $@ $<

# utils library
src/utils/libbd_utils.so: ${UTILS_OBJS}
	gcc -shared -fPIC -o $@ $^

# automatic generation of plugin stub functions
src/lib/plugin_apis/%.c: src/lib/plugin_apis/%.h
	./boilerplate_generator.py $< > $@

src/lib/blockdev.o: ${LIBRARY_FILES} ${PLUGIN_SOURCE_FILES}
	gcc -fPIC -c ${GLIB_INCLUDES} $< -o $@

src/lib/libblockdev.so: src/lib/blockdev.o
	gcc -shared -fPIC -o $@ $<

BlockDev-1.0.gir: src/utils/libbd_utils.so src/lib/libblockdev.so ${LIBRARY_FILES}
	LD_LIBRARY_PATH=src/lib/:src/utils/ g-ir-scanner `pkg-config --cflags --libs glib-2.0 gobject-2.0 libcryptsetup` --library=blockdev -I src/lib/ -L src/utils -lbd_utils -lm -L src/lib/ --identifier-prefix=BD --symbol-prefix=bd --namespace BlockDev --nsversion=1.0 -o $@ --warn-all ${LIBRARY_FILES} ${PLUGIN_HEADER_FILES}

BlockDev-1.0.typelib: BlockDev-1.0.gir
	g-ir-compiler -o $@ $<

test-from-python: src/lib/libblockdev.so ${PLUGIN_LIBS} BlockDev-1.0.typelib
	GI_TYPELIB_PATH=. LD_LIBRARY_PATH=src/plugins/:src/lib/:src/utils python -c 'from gi.repository import BlockDev; BlockDev.init(None); print BlockDev.lvm_get_max_lv_size()'

run-ipython: src/lib/libblockdev.so ${PLUGIN_LIBS} BlockDev-1.0.typelib
	GI_TYPELIB_PATH=. LD_LIBRARY_PATH=src/plugins/:src/lib/:src/utils/ ipython

run-root-ipython: src/lib/libblockdev.so ${PLUGIN_LIBS} BlockDev-1.0.typelib
	sudo GI_TYPELIB_PATH=. LD_LIBRARY_PATH=src/plugins/:src/lib/:src/utils/ ipython

test: src/utils/libbd_utils.so src/lib/libblockdev.so ${PLUGIN_LIBS} BlockDev-1.0.typelib
	@echo
	@sudo GI_TYPELIB_PATH=. LD_LIBRARY_PATH=src/plugins/:src/lib/:src/utils/ PYTHONPATH=.:tests/ \
		python -m unittest discover -v -s tests/ -p '*_test.py'

fast-test: src/utils/libbd_utils.so src/lib/libblockdev.so ${PLUGIN_LIBS} BlockDev-1.0.typelib
	@echo
	@sudo SKIP_SLOW= GI_TYPELIB_PATH=. LD_LIBRARY_PATH=src/plugins/:src/lib/:src/utils/ PYTHONPATH=.:tests/ \
		python -m unittest discover -v -s tests/ -p '*_test.py'

clean:
	-rm BlockDev-1.0.gir
	-rm BlockDev-1.0.typelib
	-rm src/lib/plugin_apis/*.c
	-rm src/lib/test_library
	-rm src/plugins/test_loop
	-rm src/plugins/test_lvm
	-rm src/plugins/test_swap
	-rm src/utils/test_sizes
	find . -name '*.o' -exec rm {} \;
	find . -name '*.so' -exec rm {} \;
	find . -name '*.pyc' -exec rm -f {} \;
	find . -name '*.pyo' -exec rm -f {} \;
