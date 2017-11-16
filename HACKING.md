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

 - [ ] Update the documentation by rsyncing the contents of the *docs/html*
       folder elsewhere, switching to the *gh-pages* branch, rsyncing the
       contents back and commiting it as an update of the docs for the new
       release.
