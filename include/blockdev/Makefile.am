all-local:
	for header in ${srcdir}/../../src/plugins/*.h; do ln -sf $${header} ./; done
	for header in ${srcdir}/../../src/utils/*.h; do ln -sf $${header} ./; done
	for header in ${srcdir}/../../src/lib/*.h; do ln -sf $${header} ./; done

clean-local:
	rm -f *.h

MAINTAINERCLEANFILES = Makefile.in
