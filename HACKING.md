CODING STYLE
============

 - Please follow the coding style already used.

 - Spaces, not tabs, are used (except for Makefiles).


MAKING RELEASES
===============

 - [ ] ``sudo git clean -xdf``

 - [ ] ``./autogen.sh && ./configure``

 - [ ] ``make bumpver``

 - [ ] Add a new entry to the *NEWS.rst* file (full list of changes should be
       generated with ``make shortlog``).

 - [ ] Commit all the changes as *New version - $VERSION*.

 - [ ] ``make release`` (requires a GPG key to sign the tag)

 - [ ] ``git push && git push --tags``

 - [ ] Edit the new release (for the new tag) at GitHub and:

   - [ ] add some detailed information about it (from the *NEWS.rst*) file to it,

   - [ ] upload the tarball created above (``make release``) to the release.

 - [ ] Generate documentation for the Python bindings as described below and copy
       it to *docs/html*

 - [ ] Update the documentation by rsyncing the contents of the *docs/html*
       folder elsewhere, switching to the *gh-pages* branch, rsyncing the
       contents back and committing it as an update of the docs for the new
       release.


Generating Python Bindings Documentation
========================================

 Documentation for Python bindings is generated using [pgi-docgen](https://github.com/pygobject/pgi-docgen). This unfortunately works only on Debian so we are using a custom Docker image to build the documentation:

1. Go to the *misc* directory.
1. Build new image using the provided *Dockerfile*

      `$ buildah bud --tag debian-docs-builder -f PythonDocs.Dockerfile .`

      This will create a new Debian Testing based image and build the documentation in it from latest libblockdev.
1. Create container from the created image

      `$ buildah from localhost/debian-docs-builder`
1. Mount the container

      `$ buildah unshare`

      `# mnt=$(buildah mount debian-docs-builder-working-container)`

1. Copy generated documentation from the container to *docs/html*

      `# cp -R $mnt/root/pgi-docgen/_docs/_build/BlockDev-2.0 <path_to_libblockdev>/docs/html`

      `# cp -R $mnt/root/pgi-docgen/_docs/_build/_static <path_to_libblockdev>/docs/html`

1. Manually fix few issues in the *BlockDev-2.0/index.html*

      - Fix version of libblockdev (pgi-docgen can't detect it so it uses version from Debian package).
      - Remove link to dependencies (we are not copying documentation generated for GLib and GObject because it more than 100 MB).
