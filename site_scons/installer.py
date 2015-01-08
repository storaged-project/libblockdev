# modified code from http://www.scons.org/wiki/Installer

""" installer

This module defines a minimal installer for scons build scripts.  It is aimed
at *nix like systems, but I guess it could easily be adapted to other ones as
well.
"""

import os
import platform
import site
import SCons.Defaults

class Installer:
    """ A basic installer. """
    def __init__(self, env):
        """ Initialize the installer.

        :param configuration: A dictionary containing the configuration.
        :param env: The installation environment.
        """

        self._prefix = env.get('PREFIX', "/usr")
        self._eprefix = env.get('EPREFIX', self._prefix)
        self._bindir = env.get('BINDIR', os.path.join(self._eprefix, "bin"))
        if platform.architecture()[0] == "64bit":
            self._libdir = env.get('BUILD_LIBDIR', os.path.join(self._eprefix, "lib64"))
        else:
            self._libdir = env.get('BUILD_LIBDIR', os.path.join(self._eprefix, "lib"))
        self._includedir = env.get('INCLUDEDIR', os.path.join(self._prefix, "include"))
        self._pkg_config_dir = os.path.join(self._libdir, "pkgconfig")
        self._sharedir = os.path.join(self._prefix, "share")
        site_dirs = env.get('SITEDIRS', site.getsitepackages()[0])
        site_dirs = site_dirs.split(",")
        if len(site_dirs) > 1:
            self._py2_site = site_dirs[0]
            self._py3_site = site_dirs[1]
        else:
            self._py2_site = site_dirs[0]
            self._py3_site = None

        self._env = env

    def Add(self, destdir, name, basedir="", perm=0644):
        destination = os.path.join(destdir, basedir)
        obj = self._env.Install(destination, name)
        self._env.Alias("install", destination)
        for i in obj:
            self._env.AddPostAction(i, SCons.Defaults.Chmod(str(i), perm))

    def AddProgram(self, program):
        """ Install a program.

        :param program: The program to install.
        """

        self.Add(self._bindir, program, perm=0755)

    def AddLibrary(self, library, basedir=""):
        """ Install a library.

        :param library: the library to install.
        """

        destination = os.path.join(self._libdir, basedir)
        obj = self._env.InstallVersionedLib(destination, library)
        self._env.Alias("install", destination)
        for i in obj:
            self._env.AddPostAction(i, SCons.Defaults.Chmod(str(i), 0755))

    def AddHeader(self, header, basedir=""):
        self.Add(self._includedir, header, basedir)

    def AddGirFile(self, gir_file):
        self.Add(os.path.join(self._sharedir, "gir-1.0"), gir_file)

    def AddTypelib(self, typelib):
        self.Add(os.path.join(self._libdir, "girepository-1.0"), typelib)

    def AddPkgConfig(self, pkg_config):
        self.Add(self._pkg_config_dir, pkg_config)

    def AddGIOverrides(self, gi_overrides):
        self.Add(os.path.join(self._py2_site, "gi/overrides/"), gi_overrides)
        if self._py3_site:
            self.Add(os.path.join(self._py3_site, "gi/overrides/"), gi_overrides)
