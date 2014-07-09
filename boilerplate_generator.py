#!/usr/bin/python3

"""
Simple helper script generating boilerplate code for a set of functions in order
to make them dynamically loaded and GObject-introspectable.

"""

from collections import namedtuple
import re
import sys
import os

FuncInfo = namedtuple("FuncInfo", ["name", "doc", "rtype", "args"])
FUNC_SPEC_RE = re.compile(r'(?P<rtype>\**\s*\w+\s*\**)'
                          r'\s*'
                          r'(?P<name>\w+)'
                          r'\s*\('
                          r'(?P<args>[\w*\s,]*)'
                          r'\);')

def gather_func_info(line_iter, includes):
    name = doc = rtype = args = ""
    in_doc = False
    in_skip = False
    for line in line_iter:
        if in_skip and line.strip().startswith("/* BpG-skip-end"):
            in_skip = False
        elif in_skip or line.strip().startswith("/* BpG-skip"):
            in_skip = True
        elif line.strip() == "" or line.strip().startswith("//") or line.strip().startswith("/* "):
            # whitespace line or ignored comment, skip it
            continue
        elif line.strip().startswith("#include"):
            includes.append(line.strip()[8:])
        elif line.strip().startswith("/**"):
            doc += line
            in_doc = True
        elif line.strip().endswith("*/") and in_doc:
            doc += line
            in_doc = False
        else:
            match = FUNC_SPEC_RE.match(line.strip())
            if match:
                fields = match.groupdict()
                name = fields["name"]
                rtype = fields["rtype"]
                args = fields["args"]

                # nothing more for this function
                break
            elif in_doc:
                doc += line
            else:
                msg = "Skipping unknown line: '{0}'".format(line.rstrip("\n"))
                print(msg, file=sys.stderr)
    else:
        return None

    return FuncInfo(name, doc, rtype, args)

def process_file(fobj):
    includes = []

    line_iter = iter(fobj)
    fn_infos = list()

    fn_info = gather_func_info(line_iter, includes)
    while fn_info:
        fn_infos.append(fn_info)
        fn_info = gather_func_info(line_iter, includes)

    return includes, fn_infos

def get_arg_names(args):
    if not args.strip():
        return []

    typed_args = args.split(",")
    starred_names = (typed_arg.split()[-1] for typed_arg in typed_args)

    return [starred_name.strip("* ") for starred_name in starred_names]

def get_func_boilerplate(fn_info):
    call_args_str = ", ".join(get_arg_names(fn_info.args))

    if "int" in fn_info.rtype:
        default_ret = "0"
    elif "float" in fn_info.rtype:
        default_ret = "0.0"
    elif "bool" in fn_info.rtype:
        default_ret = "FALSE"
    else:
        default_ret = "NULL"

    # first add the stub function doing nothing and just reporting error
    ret = ("{0.rtype} {0.name}_stub ({0.args}) {{\n" +
           "    g_critical(\"The function '{0.name}' called, but not implemented!\");\n" +
           "    return {1};\n"
           "}}\n\n").format(fn_info, default_ret)

    # then add a variable holding a reference to the dynamically loaded function
    # (if any) initialized to the stub
    ret += "{0.rtype} (*_{0.name}) ({0.args}) = {0.name}_stub;\n\n".format(fn_info)

    # then add a documented function calling the dynamically loaded one via the
    # reference
    ret += ("{0.doc}{0.rtype} {0.name} ({0.args}) {{\n" +
            "    return _{0.name} ({1});\n" +
            "}}\n\n\n").format(fn_info, call_args_str)

    return ret

def get_includes_str(includes):
    if not includes:
        return ""

    ret = ""
    for include in includes:
        ret += "#include%s\n" % include
    ret += "\n"

    return ret

def get_funcs_info(fn_infos, module_name):
    ret = "static const gchar const * const {0}_functions[] = {{\n".format(module_name)
    for info in fn_infos:
        ret += '    "{0.name}",\n'.format(info)
    ret += "    NULL};\n\n"

    ret += ("gchar const * const * get_{0}_functions (void) {{\n".format(module_name) +
            "    return {0}_functions;\n".format(module_name) +
            "}\n\n")

    ret += "static const guint8 {0}_num_functions = {1};\n\n".format(module_name, len(fn_infos))
    ret += ("guint8 get_{0}_num_functions (void) {{\n".format(module_name) +
            "    return {0}_num_functions;\n".format(module_name) +
            "}\n\n")

    return ret

def get_loading_func(fn_infos, module_name):
    # TODO: only error on functions provided by the plugin that fail to load
    # TODO: implement the 'gchar **errors' argument
    ret =  'void* load_{0}_from_plugin(gchar *so_name) {{\n'.format(module_name)
    ret += '    void *handle = NULL;\n'
    ret += '    char *error = NULL;\n\n'

    ret += '    handle = dlopen(so_name, RTLD_LAZY);\n'
    ret += '    if (!handle) {\n'
    ret += '        g_warning("failed to load module {0}: %s", dlerror());\n'.format(mod_name)
    ret += '        return NULL;\n'
    ret += '    }\n\n'

    for info in fn_infos:
        # clear any previous error and load the function
        ret += '    dlerror();\n'
        ret += '    * (void**) (&_{0.name}) = dlsym(handle, "{0.name}");\n'.format(info)
        ret += '    if ((error = dlerror()) != NULL)\n'
        ret += '        g_warning("failed to load {0.name}: %s", error);\n\n'.format(info)

    ret += '    return handle;\n'
    ret += '}\n\n'

    return ret

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Needs a file name, exitting.", file=sys.stderr)
        print("Usage: %s FILE_NAME", sys.argv[0])
        sys.exit(1)

    file_name = os.path.basename(sys.argv[1])
    mod_name, dot, _ext = file_name.partition(".")
    if not dot:
        print("Invalid file given, needs to be in MODNAME.[ch] format", file=sys.stderr)
        sys.exit(2)

    includes, fn_infos = process_file(open(sys.argv[1], "r"))
    print(get_funcs_info(fn_infos, mod_name), end='')
    for info in fn_infos:
        print(get_func_boilerplate(info), end='')
    print(get_loading_func(fn_infos, mod_name), end='')
