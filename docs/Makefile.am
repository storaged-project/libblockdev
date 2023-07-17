all-local: html-doc.stamp

html-doc.stamp: ${srcdir}/libblockdev-docs.xml ${srcdir}/libblockdev-sections.txt ${srcdir}/3.0-api-changes.xml $(wildcard ${srcdir}/../src/plugins/*.[ch]) $(wildcard ${srcdir}/../src/lib/*.[ch]) $(wildcard ${srcdir}/../src/utils/*.[ch])
	touch ${builddir}/html-doc.stamp
	test "${builddir}" = "${srcdir}" || cp ${srcdir}/libblockdev-sections.txt ${srcdir}/libblockdev-docs.xml ${builddir}
	gtkdoc-scan --rebuild-types --module=libblockdev --source-dir=${srcdir}/../src/plugins/ --source-dir=${srcdir}/../src/lib/ --source-dir=${srcdir}/../src/utils/ --ignore-headers="${srcdir}/../src/plugins/check_deps.h ${srcdir}/../src/plugins/dm_logging.h ${srcdir}/../src/plugins/vdo_stats.h ${srcdir}/../src/plugins/fs/common.h"
	gtkdoc-mkdb --module=libblockdev --output-format=xml --source-dir=${srcdir}/../src/plugins/ --source-dir=${srcdir}/../src/lib/ --source-dir=${srcdir}/../src/utils/ --source-suffixes=c,h
	test -d ${builddir}/html || mkdir ${builddir}/html
	(cd ${builddir}/html; gtkdoc-mkhtml libblockdev ${builddir}/../libblockdev-docs.xml)
	gtkdoc-fixxref --module=libblockdev --module-dir=html  --html-dir=/usr/share/gtk-doc/html

clean-local:
	-rm -rf ${builddir}/html
	-rm -rf ${builddir}/xml
	test ! -f ${builddir}/html-doc.stamp || rm ${builddir}/html-doc.stamp
	test "${builddir}" = "${srcdir}" || rm -f ${builddir}/libblockdev-sections.txt ${builddir}/libblockdev-docs.xml ${builddir}/3.0-api-changes.xml

install-data-local:
	test -d ${DESTDIR}${datadir}/gtk-doc/html/libblockdev || mkdir -p ${DESTDIR}${datadir}/gtk-doc/html/libblockdev
	install -m0644 ${builddir}/html/* ${DESTDIR}${datadir}/gtk-doc/html/libblockdev/

uninstall-local:
	-rm -rf ${DESTDIR}${datadir}/gtk-doc/html/libblockdev/

dist_noinst_DATA = ${srcdir}/libblockdev-sections.txt ${srcdir}/libblockdev-docs.xml ${srcdir}/3.0-api-changes.xml

CLEANFILES = html.stamp sgml.stamp \
	libblockdev-decl-list.txt \
	libblockdev-decl.txt \
	libblockdev-overrides.txt \
	libblockdev-undeclared.txt \
	libblockdev-undocumented.txt \
	libblockdev-unused.txt \
	libblockdev.types

MAINTAINERCLEANFILES = Makefile.in libblockdev-docs.xml
