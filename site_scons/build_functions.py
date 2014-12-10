import os
import subprocess
import platform
from boilerplate_generator import generate_source_header

__all__ = ["generate_boilerplate_files", "generate_gir_file", "generate_pc_file"]

def generate_boilerplate_files(target, source, env):
    return generate_source_header(str(source[0]), os.path.dirname(str(target[0])))

def generate_gir_file(target, source, env):
    ld_lib_path = env.get('LD_LIBRARY_PATH', None)
    if not ld_lib_path:
        env['LD_LIBRARY_PATH'] = ""
    for lib_path in env['LIBPATH']:
        if lib_path not in env['LD_LIBRARY_PATH']:
            env['LD_LIBRARY_PATH'] += ":%s" % lib_path

    lib_name = env['LIB_NAME']
    ident_prefix = env['IDENTIFIER_PREFIX']
    symb_prefix = env['SYMBOL_PREFIX']
    namespace = env['NAMESPACE']
    ns_version = env['NS_VERSION']

    argv = ["g-ir-scanner", "--warn-error", "--warn-all"]
    for lib in env['LIBS']:
        argv.append("-l%s" % lib)
    for lib_path in env['LIBPATH']:
        argv.append("-L%s" % lib_path)
    for cpp_path in env['CPPPATH']:
        argv.append("-I%s" % cpp_path)
    argv.append("--library=%s" % lib_name)
    argv.append("--identifier-prefix=%s" % ident_prefix)
    argv.append("--symbol-prefix=%s" % symb_prefix)
    argv.append("--namespace=%s" % namespace)
    argv.append("--nsversion=%s" % ns_version)
    argv.append("-o%s" % target[0])
    argv += [str(src) for src in source]

    proc_env = os.environ.copy()
    proc_env["LD_LIBRARY_PATH"] = env['LD_LIBRARY_PATH']
    proc = subprocess.Popen(argv, stdout=subprocess.PIPE, stderr=subprocess.PIPE, env=proc_env)
    out, err = proc.communicate()

    if proc.returncode != 0:
        print out
        print err

    # 0 or None means OK, anything else means NOK
    return proc.returncode

def generate_pc_file(target, source, env):
    """Generate the .pc (pkg-config) file"""

    name = env['NAME']
    description = env.get('DESCRIPTION', "No description")
    requires = env.get('PC_REQUIRES', [])
    url = env.get('URL', "")
    version = env.get('SHLIBVERSION', "1.0.0")
    cflags = " ".join(env.get('PC_CPPPATH', []))
    libs = " ".join(env.get('PC_LIBPATH', [])) + "-l%s" % env['LIB_NAME']

    if platform.architecture()[0] == "64bit":
        libdir = "lib64"
    else:
        libdir = "lib"

    with open(str(target[0]), "w") as pc_file:
        pc_file.write("prefix=%s\n" % env.get('PREFIX', "/usr"))
        pc_file.write("exec_prefix=%s\n" % env.get('EPREFIX', "${prefix}"))
        pc_file.write("includedir=%s\n" % env.get('INCLUDEDIR', "${prefix}/include"))
        pc_file.write("libdir=%s\n\n" % env.get('LIBDIR', "${exec_prefix}/%s" % libdir))

        pc_file.write("Name: %s\n" % name)
        pc_file.write("Description: %s\n" % description)
        if url:
            pc_file.write("URL: %s\n" % url)
        pc_file.write("Version: %s\n" % version)
        for req in requires:
            pc_file.write("Requires: %s\n" % req)
        pc_file.write("Cflags: %s\n" % cflags)
        pc_file.write("Libs: %s\n" % libs)
