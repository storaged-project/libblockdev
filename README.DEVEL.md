libblockdev developer documentation
====================================

The purpose of this document is to describe the internals of the *libblockdev*
library for future developers and maintainers.

The library itself (the ``src/lib/blockdev.c.in`` and ``src/lib/plugins.c``
files) is just a thin wrapper around its plugins which take care about the
actual functionality. Each plugin is defined by an ``.api`` file in the
``src/lib/plugin_apis`` directory. There might be multiple implementations for
each plugin like for example the ``lvm.c`` and ``lvm-dbus.c`` files both
providing implementations of the *LVM* plugin. Each implementation of some
plugin is compiled as a standalone shared library and can be used separately
without the *libblockdev* library itself if desired.

However, it is strongly recommended to use the library and use one or more of
its initialization functions to either load all the plugins or just a desired
subset. That takes care of the plugins' check and initialization code to be
executed as well as resolution of priorities and fallback-based loading of
multiple implementations for plugins (if that's the case). As was already
mentioned above, the library is just a thin wrapper so there's no point in
trying to bypass it and use the plugins directly as standalone libraries.

Coding style
-------------

 - Please follow the coding style already used.
 - Spaces, not tabs, are used (except for Makefiles).

Language bindings
------------------

Though the library uses the so called *GObject introspection* (*GI*) framework
to provide bindings for languages other than *C*, it's not an object-oriented
library and it makes little to no use of the *GObject* type system. The only
exception are a few structures registered as *GBoxedType* types which are
structures together with related ``free()`` and ``copy()`` functions
defined. The framework itself provides an interface for all the other (basic)
types being used like strings, numbers, etc. as well as for error reporting
based on the *GError* mechanism (translated to language-native error/exception
reporting and handling).

In order to make the *GObject introspection* work together with dynamic loading
and fail-safe execution of functions that are not provided by any loaded plugin,
the library defines its own stub functions that just report an error if
called. Such functions are generated automatically from the ``.api`` files by
the ``scripts/boilerplate_generator.py`` script and when plugins are loaded,
these stubs are replaced by the real functions provided by the plugins. The
helper script also generates functions used for loading plugins (all their
functions have to be loaded one by one).

Adding a new function
----------------------

Thus if a new function is being added to any of the plugins:

1. the definition of the function has to be added to the particular ``.api``
   file

2. the function prototype has to be added to the particular header file

3. implementation(s) of the function have to be added to one or more plugins

4. plugin's soname version has to be adapted to the change(s)

5. the function has to be added to the particular place in the documentation
   (the ``docs/libblockdev-sections.txt`` file)

Adding a new plugin
--------------------

If a new plugin is being added:

1. the definitions of its functions have to be put into a new ``.api`` file

2. code in ``src/lib/blockdev.c.in`` has to be adapted to load the plugin's
   implementation and report its state

3. related definitions have to be added to the related autotools files

4. the plugin has to be added to the particular place in the documentation
   (the ``docs/libblockdev-sections.txt`` file)

See e.g. the commit a5847cd9c266d1116b87356aa1816ddb0bfc194e for more details.


If a new struct field is being added, it has to be added to the end of the
structure so that original fields stay in their places from the ABI point of
view.

Directory structure
--------------------

The directory structure is as follows:

```
  libblockdev
  ├── data                     -- data files
  │   └── conf.d               -- configuration files
  ├── dist                     -- packaging files
  ├── docs                     -- files used to generate documentation
  ├── misc                     -- miscellaneous files easing development
  ├── scripts                  -- helper scripts
  ├── src                      -- source files
  │   ├── lib                  -- library sources
  │   │   └── plugin_apis      -- plugins definitions
  │   ├── plugins              -- plugins implementations
  │   ├── python               -- python bindings
  │   │   └── gi               -- just needed for GI overrides to work
  │   │       └── overrides    -- ditto, actual sources of the GI overrides
  │   └── utils                -- sources of the utility library
  ├── tests                    -- tests
  └── tools                    -- sources of various nice tools
```

Making releases
================

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

Generating Python bindings documentation
-----------------------------------------

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

      `# cp -R $mnt/root/pgi-docgen/_docs/_build/BlockDev-3.0 <path_to_libblockdev>/docs/html`

      `# cp -R $mnt/root/pgi-docgen/_docs/_build/_static <path_to_libblockdev>/docs/html`

1. Manually fix few issues in the *BlockDev-3.0/index.html*

      - Fix version of libblockdev (pgi-docgen can't detect it so it uses version from Debian package).
      - Remove link to dependencies (we are not copying documentation generated for GLib and GObject because it more than 100 MB).
