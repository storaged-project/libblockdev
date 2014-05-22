SIZES_FILES = src/util/sizes.c src/util/sizes.h
LVM_PLUGIN_FILES = src/plugins/lvm.h src/plugins/lvm.c
LIBRARY_FILES = src/lib/blockdev.c src/lib/blockdev.h src/lib/plugins.h src/lib/plugin_apis/lvm.h

build-plugins: ${LVM_PLUGIN_FILES} ${SIZES_FILES}
	gcc -c -fPIC -I src/util/ -I src/plugins/ -lm `pkg-config --libs --cflags glib-2.0`\
		src/util/sizes.c src/plugins/lvm.c
	gcc -shared -o src/plugins/libbd_lvm.so lvm.o

generate-boilerplate-code: src/lib/plugin_apis/lvm.h
	./boilerplate_generator.py src/lib/plugin_apis/lvm.h > src/lib/plugin_apis/lvm.c

build-library: generate-boilerplate-code ${LIBRARY_FILES}
	gcc -fPIC -c `pkg-config --libs --cflags glib-2.0` -ldl src/lib/blockdev.c
	gcc -shared -o src/lib/libblockdev.so blockdev.o

build-introspection-data: build-library ${LIBRARY_FILES}
	LD_LIBRARY_PATH=src/lib/ g-ir-scanner `pkg-config --cflags --libs glib-2.0` --library=blockdev -I src/lib/ -L src/lib/ --identifier-prefix=BD --symbol-prefix=bd --namespace BlockDev --nsversion=1.0 -o BlockDev-1.0.gir --warn-all src/lib/blockdev.h src/lib/blockdev.c src/lib/plugins.h src/lib/plugin_apis/lvm.h
	g-ir-compiler -o BlockDev-1.0.typelib BlockDev-1.0.gir

test-sizes: ${SIZES_FILES}
	gcc -DTESTING_SIZES -o test_sizes -I src/util/ -lm `pkg-config --libs --cflags glib-2.0`\
		src/util/sizes.c
	@echo "***Running tests***"
	./test_sizes
	@rm test_sizes

test-lvm-plugin: ${LVM_PLUGIN_FILES} ${SIZES_FILES}
	gcc -DTESTING_LVM -o test_lvm_plugin -I src/util/ -I src/plugins/ -lm `pkg-config --libs --cflags glib-2.0`\
		src/util/sizes.c src/plugins/lvm.c
	@echo "***Running tests***"
	./test_lvm_plugin
	@rm test_lvm_plugin

test-library: generate-boilerplate-code build-plugins
	gcc -DTESTING_LIB -o test_library `pkg-config --libs --cflags glib-2.0` -ldl src/lib/blockdev.c
	@echo "***Running tests***"
	LD_LIBRARY_PATH=src/plugins/ ./test_library
	@rm test_library

test-from-python: build-introspection-data build-library build-plugins
	GI_TYPELIB_PATH=. LD_LIBRARY_PATH=src/plugins/:src/lib/ python -c 'from gi.repository import BlockDev; BlockDev.init(None); print BlockDev.lvm_get_max_lv_size()'
