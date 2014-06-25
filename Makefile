SIZES_FILES = src/utils/sizes.c src/utils/sizes.h
LVM_PLUGIN_FILES = src/plugins/lvm.h src/plugins/lvm.c
SWAP_PLUGIN_FILES = src/plugins/swap.h src/plugins/swap.c
LOOP_PLUGIN_FILES = src/plugins/loop.h src/plugins/loop.c
CRYPTO_PLUGIN_FILES = src/plugins/crypto.h src/plugins/crypto.c
LIBRARY_FILES = src/lib/blockdev.c src/lib/blockdev.h src/lib/plugins.h src/lib/plugin_apis/lvm.h

build-plugins: ${LVM_PLUGIN_FILES} ${SWAP_PLUGIN_FILES} ${LOOP_PLUGIN_FILES} ${SIZES_FILES}
	gcc -c -Wall -Wextra -Werror -fPIC -I src/utils/ -I src/plugins/ -lm `pkg-config --libs --cflags glib-2.0 gobject-2.0`\
		src/utils/sizes.c src/plugins/lvm.c
	gcc -shared -o src/plugins/libbd_lvm.so lvm.o

	gcc -c -Wall -Wextra -Werror -fPIC -I src/plugins/ -lm `pkg-config --libs --cflags glib-2.0`\
		src/plugins/swap.c
	gcc -shared -o src/plugins/libbd_swap.so swap.o

	gcc -c -Wall -Wextra -Werror -fPIC -I src/plugins/ -lm `pkg-config --libs --cflags glib-2.0`\
		src/plugins/loop.c
	gcc -shared -o src/plugins/libbd_loop.so loop.o

	gcc -c -Wall -Wextra -Werror -fPIC -I src/plugins/ -lm `pkg-config --libs --cflags glib-2.0 libcryptsetup`\
		src/plugins/crypto.c
	gcc -shared -o src/plugins/libbd_crypto.so crypto.o

generate-boilerplate-code: src/lib/plugin_apis/lvm.h src/lib/plugin_apis/swap.h
	./boilerplate_generator.py src/lib/plugin_apis/lvm.h > src/lib/plugin_apis/lvm.c
	./boilerplate_generator.py src/lib/plugin_apis/swap.h > src/lib/plugin_apis/swap.c
	./boilerplate_generator.py src/lib/plugin_apis/loop.h > src/lib/plugin_apis/loop.c
	./boilerplate_generator.py src/lib/plugin_apis/crypto.h > src/lib/plugin_apis/crypto.c

build-library: generate-boilerplate-code ${LIBRARY_FILES}
	gcc -fPIC -c `pkg-config --libs --cflags glib-2.0` -ldl src/lib/blockdev.c
	gcc -shared -o src/lib/libblockdev.so blockdev.o

build-introspection-data: build-library ${LIBRARY_FILES}
	LD_LIBRARY_PATH=src/lib/ g-ir-scanner `pkg-config --cflags --libs glib-2.0 gobject-2.0 libcryptsetup` --library=blockdev -I src/lib/ -L src/lib/ --identifier-prefix=BD --symbol-prefix=bd --namespace BlockDev --nsversion=1.0 -o BlockDev-1.0.gir --warn-all src/lib/blockdev.h src/lib/blockdev.c src/lib/plugins.h src/lib/plugin_apis/lvm.h src/lib/plugin_apis/swap.h src/lib/plugin_apis/loop.h src/lib/plugin_apis/crypto.h
	g-ir-compiler -o BlockDev-1.0.typelib BlockDev-1.0.gir

test-sizes: ${SIZES_FILES}
	gcc -Wall -DTESTING_SIZES -o test_sizes -I src/utils/ -lm `pkg-config --libs --cflags glib-2.0`\
		src/utils/sizes.c
	@echo "***Running tests***"
	./test_sizes
	@rm test_sizes

test-lvm-plugin: ${LVM_PLUGIN_FILES} ${SIZES_FILES}
	gcc -DTESTING_LVM -o test_lvm_plugin -I src/utils/ -I src/plugins/ -lm `pkg-config --libs --cflags glib-2.0 gobject-2.0`\
		src/utils/sizes.c src/plugins/lvm.c
	@echo "***Running tests***"
	./test_lvm_plugin
	@rm test_lvm_plugin

test-swap-plugin: ${SWAP_PLUGIN_FILES}
	gcc -DTESTING_SWAP -o test_swap_plugin -I src/plugins/ -lm `pkg-config --libs --cflags glib-2.0` src/plugins/swap.c
	@echo "***Running tests***"
	./test_swap_plugin
	@rm test_swap_plugin

test-loop-plugin: ${LOOP_PLUGIN_FILES}
	gcc -DTESTING_LOOP -o test_loop_plugin -I src/plugins/ -lm `pkg-config --libs --cflags glib-2.0` src/plugins/loop.c
	@echo "***Running tests***"
	./test_loop_plugin
	@rm test_loop_plugin

test-library: generate-boilerplate-code build-plugins
	gcc -DTESTING_LIB -o test_library `pkg-config --libs --cflags glib-2.0 gobject-2.0` -ldl src/lib/blockdev.c
	@echo "***Running tests***"
	LD_LIBRARY_PATH=src/plugins/ ./test_library
	@rm test_library

test-from-python: build-library build-plugins build-introspection-data
	GI_TYPELIB_PATH=. LD_LIBRARY_PATH=src/plugins/:src/lib/ python -c 'from gi.repository import BlockDev; BlockDev.init(None); print BlockDev.lvm_get_max_lv_size()'

run-ipython: build-library build-plugins build-introspection-data
	GI_TYPELIB_PATH=. LD_LIBRARY_PATH=src/plugins/:src/lib/ ipython

run-root-ipython: build-library build-plugins build-introspection-data
	sudo GI_TYPELIB_PATH=. LD_LIBRARY_PATH=src/plugins/:src/lib/ ipython

test: build-library build-plugins build-introspection-data
	@echo
	@sudo GI_TYPELIB_PATH=. LD_LIBRARY_PATH=src/plugins/:src/lib/ PYTHONPATH=.:tests/ python -m unittest discover -v -s tests/ -p '*_test.py'
