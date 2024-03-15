all-local:
	for header in ${srcdir}/../../src/plugins/*.h; do ln -sf $${header} ./; done
	for header in ${srcdir}/../../src/plugins/nvme/nvme.h; do ln -sf $${header} ./; done
	for header in ${srcdir}/../../src/plugins/smart/smart.h; do ln -sf $${header} ./; done
	for header in ${srcdir}/../../src/utils/*.h; do ln -sf $${header} ./; done
	for header in ${srcdir}/../../src/lib/*.h; do ln -sf $${header} ./; done
	mkdir -p fs;
	cd fs; for header in ${srcdir}/../../../src/plugins/fs/*.h; do ln -sf $${header} ./; done

clean-local:
	rm -f *.h
	rm -rf fs

MAINTAINERCLEANFILES = Makefile.in
