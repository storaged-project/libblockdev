PKGNAME=libblockdev
VERSION=$(shell awk '/Version:/ { print $$2 }' dist/$(PKGNAME).spec)
RELEASE=$(shell awk '/Release:/ { print $$2 }' dist/$(PKGNAME).spec | sed -e 's|%.*$$||g')
TAG=libblockdev-$(VERSION)-$(RELEASE)

PLUGIN_TESTS = test-btrfs test-lvm test-loop test-swap

all: build documentation

build:
	scons -Q build

install:
	scons -Q --prefix=${PREFIX} --sitedirs=${SITEDIRS} install
	-mkdir -p ${PREFIX}/usr/share/gtk-doc/html/libblockdev/
	install -m0644 build/docs/html/* ${PREFIX}/usr/share/gtk-doc/html/libblockdev/

uninstall:
	scons -Q -c install
	-rm -rf ${PREFIX}/usr/share/gtk-doc/html/libblockdev/

test_%:
	scons -Q build/$@

test-%: test_%
	@echo "***Running tests***"
	LD_LIBRARY_PATH=build/ build/$<

%.so:
	scons -Q build/$@

BlockDev-0.1.gir:
	scons -Q build/$@

BlockDev-0.1.typelib:
	scons -Q build/$@

plugins-test: ${PLUGIN_TESTS}

test-from-python: build
	GI_TYPELIB_PATH=build LD_LIBRARY_PATH=build PYTHONPATH=src/python python -c \
         'import gi.overrides;\
		 gi.overrides.__path__.insert(0, "src/python");\
         from gi.repository import BlockDev;\
         BlockDev.init();\
         BlockDev.reinit();\
         print BlockDev.lvm_get_max_lv_size()'

run-ipython: build
	GI_TYPELIB_PATH=build/ LD_LIBRARY_PATH=build PYTHONPATH=src/python G_MESSAGES_DEBUG=all ipython

run-root-ipython: build
	sudo GI_TYPELIB_PATH=build/ LD_LIBRARY_PATH=build PYTHONPATH=src/python G_MESSAGES_DEBUG=all ipython

test: build
	@echo
	@sudo GI_TYPELIB_PATH=build LD_LIBRARY_PATH=build PYTHONPATH=.:tests/:src/python \
		python -m unittest discover -v -s tests/ -p '*_test.py'

fast-test: build
	@echo
	@sudo SKIP_SLOW= GI_TYPELIB_PATH=build LD_LIBRARY_PATH=build PYTHONPATH=.:tests/:src/python \
		python -m unittest discover -v -s tests/ -p '*_test.py'

documentation: $(wildcard src/plugins/*.[ch]) $(wildcard src/lib/*.[ch]) $(wildcard src/utils/*.[ch])
	-mkdir -p build/docs
	cp docs/libblockdev-docs.xml docs/libblockdev-sections.txt build/docs
	(cd build/docs;	gtkdoc-scan --rebuild-types --module=libblockdev --source-dir=../../src/plugins/ --source-dir=../../src/lib/ --source-dir=../../src/utils/)
	(cd build/docs; gtkdoc-mkdb --module=libblockdev --output-format=xml --source-dir=../../src/plugins/ --source-dir=../../src/lib/ --source-dir=../../src/utils/ --source-suffixes=c,h)
	-mkdir build/docs/html
	(cd build/docs/html; gtkdoc-mkhtml libblockdev ../libblockdev-docs.xml)
	(cd build/docs/; gtkdoc-fixxref --module=libblockdev --module-dir=html  --html-dir=/usr/share/gtk-doc/html)

clean:
	-rm -rf build/
	find . -name '*.pyc' -exec rm -f {} \;
	find . -name '*.pyo' -exec rm -f {} \;

tag:
	git tag -a -s -m "Tag as $(TAG)" -f $(TAG)
	@echo "Tagged as $(TAG)"

rpmlog:
	@git log --pretty="format:- %s (%ae)" $(TAG).. |sed -e 's/@.*)/)/'
	@echo

bumpver:
	@NEWSUBVER=$$((`echo $(VERSION) |cut -d . -f 2` + 1)) ; \
	NEWVERSION=`echo $(VERSION).$$NEWSUBVER |cut -d . -f 1,3` ; \
	DATELINE="* `date "+%a %b %d %Y"` `git config user.name` <`git config user.email`> - $$NEWVERSION-1"  ; \
	cl=`grep -n %changelog dist/libblockdev.spec |cut -d : -f 1` ; \
	tail --lines=+$$(($$cl + 1)) dist/libblockdev.spec > speclog ; \
	(head -n $$cl dist/libblockdev.spec ; echo "$$DATELINE" ; make --quiet rpmlog 2>/dev/null ; echo ""; cat speclog) > dist/libblockdev.spec.new ; \
	mv dist/libblockdev.spec.new dist/libblockdev.spec ; rm -f speclog ; \
	sed -ri "s/Version:(\\s+)$(VERSION)/Version:\\1$$NEWVERSION/" dist/libblockdev.spec ; \

archive:
	git archive --format=tar.gz --prefix=$(PKGNAME)-$(VERSION)/ -o $(PKGNAME)-$(VERSION).tar.gz $(TAG)

local:
	git archive --format=tar.gz --prefix=$(PKGNAME)-$(VERSION)/ -o $(PKGNAME)-$(VERSION).tar.gz HEAD

srpm: local
	rpmbuild -ts --nodeps $(PKGNAME)-$(VERSION).tar.gz
	rm -f $(PKGNAME)-$(VERSION).tar.gz

rpm: local
	rpmbuild -tb --nodeps $(PKGNAME)-$(VERSION).tar.gz
	rm -f $(PKGNAME)-$(VERSION).tar.gz

release: tag
	$(MAKE) archive
