#!/usr/bin/python

"""
Simple helper script generating boilerplate code for a set of functions in order
to make them dynamically loaded and GObject-introspectable.

"""

from collections import namedtuple
import re
import sys
import os

FuncInfo = namedtuple("FuncInfo", ["name", "doc", "rtype", "args", "body"])
FUNC_SPEC_RE = re.compile(r'(?P<rtype>(const\s+)?\**\s*\w+\s*\**)'
                          r'\s*'
                          r'(?P<name>\w+)'
                          r'\s*\('
                          r'(?P<args>[\w*\s,]*)'
                          r'\)(;| \{)')

SIZE_CONST_DEF_RE = re.compile(r'^(?P<name_num>#define\s+\w+\s*\(\s*\d+\s+)(?P<unit>[kmgtpeKMGTPE]i?[bB])\s*\)\s*$')

KiB = 1024
MiB = 1024 * KiB
GiB = 1024 * MiB
TiB = 1024 * GiB
PiB = 1024 * TiB
EiB = 1024 * PiB
KB = 1000
MB = 1000 * KB
GB = 1000 * MB
TB = 1000 * GB
PB = 1000 * TB
EB = 1000 * PB

UNIT_MULTS = {"KiB": KiB, "MiB": MiB, "GiB": GiB, "TiB": TiB, "PiB": PiB, "EiB": EiB,
              "KB": KB, "MB": MB, "GB": GB, "TB": TB, "PB": PB, "EB": EB}

# overrides for function prefixes not matching the modules' names
MOD_FNAME_OVERRIDES = {"mdraid": "md"}

def expand_size_constants(definitions):
    """
    Expand macros that define size constants (e.g. '#define DEFAULT_PE_SIZE (4 MiB)').

    :param str definitions: lines from a source file potentially containing
                            definitions of size constants.
    :returns: definitions with size constants expanded

    """

    ret = ""
    for def_line in definitions.splitlines():
        match = SIZE_CONST_DEF_RE.match(def_line)
        if match:
            fields = match.groupdict()
            unit = fields["unit"]
            if unit in UNIT_MULTS:
                name_num = fields["name_num"]
                ret += (name_num + "* " + "%dULL" % UNIT_MULTS[unit] + ")\n")
            else:
                # unknown unit, cannot expand it, just leave it as it is
                ret += (def_line + "\n")
        else:
            ret += (def_line + "\n")

    return ret

def gather_defs_and_func_info(line_iter, includes):
    name = doc = rtype = args = ""
    defs = ""
    body = ""
    in_body = False
    in_doc = False
    in_skip = False
    for line in line_iter:
        if in_skip and line.strip().startswith("/* BpG-skip-end"):
            in_skip = False
        elif in_skip or line.strip().startswith("/* BpG-skip"):
            in_skip = True
        elif line.rstrip() == "}" and in_body:
            # nothing more for this function
            break
        elif in_body:
            body += line
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

                if line.strip().endswith("{"):
                    in_body = True
                else:
                    # nothing more for this function
                    break
            elif in_doc:
                doc += line
            else:
                defs += line
    else:
        return (expand_size_constants(defs), None)

    return (expand_size_constants(defs), FuncInfo(name, doc, rtype, args, body))

def process_file(fobj):
    includes = []

    line_iter = iter(fobj)
    items = list()

    defs, fn_info = gather_defs_and_func_info(line_iter, includes)
    while fn_info:
        if defs:
            items.append(defs)
        if fn_info:
            items.append(fn_info)
        defs, fn_info = gather_defs_and_func_info(line_iter, includes)

    if defs:
        # definitions after the last function
        items.append(defs)
    return includes, items

def get_arg_names(args):
    if not args.strip():
        return []

    typed_args = args.split(",")
    starred_names = (typed_arg.split()[-1] for typed_arg in typed_args)

    return [starred_name.strip("* ") for starred_name in starred_names]

def get_func_boilerplate(fn_info):
    call_args_str = ", ".join(get_arg_names(fn_info.args))
    args_ann_unused = fn_info.args.replace(",", " G_GNUC_UNUSED,")

    if "int" in fn_info.rtype:
        default_ret = "0"
    elif "float" in fn_info.rtype:
        default_ret = "0.0"
    elif "bool" in fn_info.rtype:
        default_ret = "FALSE"
    elif fn_info.rtype.endswith("*"):
        # a pointer
        default_ret = "NULL"
    else:
        # enum or whatever
        default_ret = 0

    # first add the stub function doing nothing and just reporting error
    ret = ("static {0.rtype} {0.name}_stub ({2}) {{\n" +
           "    g_critical (\"The function '{0.name}' called, but not implemented!\");\n" +
           "    g_set_error (error, BD_INIT_ERROR, BD_INIT_ERROR_NOT_IMPLEMENTED,\n"+
           "                \"The function '{0.name}' called, but not implemented!\");\n"
           "    return {1};\n"
           "}}\n\n").format(fn_info, default_ret, args_ann_unused)

    # then add a variable holding a reference to the dynamically loaded function
    # (if any) initialized to the stub
    ret += "static {0.rtype} (*_{0.name}) ({0.args}) = {0.name}_stub;\n\n".format(fn_info)

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

def get_loading_func(fn_infos, module_name):
    # TODO: only error on functions provided by the plugin that fail to load
    # TODO: implement the 'gchar **errors' argument
    ret =  'static gpointer load_{0}_from_plugin(const gchar *so_name) {{\n'.format(module_name)
    ret += '    void *handle = NULL;\n'
    ret += '    char *error = NULL;\n'
    ret += '    gboolean (*init_fn) (void) = NULL;\n\n'

    ret += '    handle = dlopen(so_name, RTLD_LAZY);\n'
    ret += '    if (!handle) {\n'
    ret += '        bd_utils_log_format (BD_UTILS_LOG_WARNING, "failed to load module {0}: %s", dlerror());\n'.format(module_name)
    ret += '        return NULL;\n'
    ret += '    }\n\n'

    ret += '    dlerror();\n'
    ret += '    * (void**) (&init_fn) = dlsym(handle, "bd_{0}_init");\n'.format(MOD_FNAME_OVERRIDES.get(module_name, module_name))
    ret += '    if ((error = dlerror()) != NULL)\n'
    ret += '        bd_utils_log_format (BD_UTILS_LOG_DEBUG, "failed to load the init() function for {0}: %s", error);\n'.format(module_name)
    ret += '    /* coverity[dead_error_condition] */\n'  # coverity doesn't understand dlsym and thinks init_fn is NULL
    ret += '    if (init_fn && !init_fn()) {\n'
    ret += '        dlclose(handle);\n'
    ret += '        return NULL;\n'
    ret += '    }\n'
    ret += '    init_fn = NULL;\n\n'

    for info in fn_infos:
        # clear any previous error and load the function
        ret += '    dlerror();\n'
        ret += '    * (void**) (&_{0.name}) = dlsym(handle, "{0.name}");\n'.format(info)
        ret += '    if ((error = dlerror()) != NULL)\n'
        ret += '        bd_utils_log_format (BD_UTILS_LOG_WARNING, "failed to load {0.name}: %s", error);\n\n'.format(info)

    ret += '    return handle;\n'
    ret += '}\n\n'

    return ret

def get_unloading_func(fn_infos, module_name):
    ret =  'static gboolean unload_{0} (gpointer handle) {{\n'.format(module_name)
    ret += '    char *error = NULL;\n'
    ret += '    gboolean (*close_fn) (void) = NULL;\n\n'

    # revert the functions to stubs
    for info in fn_infos:
        ret += '    _{0.name} = {0.name}_stub;\n'.format(info)

    ret += '\n'
    ret += '    dlerror();\n'
    ret += '    * (void**) (&close_fn) = dlsym(handle, "bd_{0}_close");\n'.format(MOD_FNAME_OVERRIDES.get(module_name, module_name))
    ret += '    if (((error = dlerror()) != NULL) || !close_fn)\n'
    ret += '        bd_utils_log_format (BD_UTILS_LOG_DEBUG, "failed to load the close_plugin() function for {0}: %s", error);\n'.format(module_name)
    ret += '    /* coverity[dead_error_condition] */\n'  # coverity doesn't understand dlsym and thinks close_fn is NULL
    ret += '    if (close_fn) {\n'
    ret += '        close_fn();\n'
    ret += '    }\n\n'
    ret += '    return dlclose(handle) == 0;\n'
    ret += '}\n\n'

    return ret

def get_fn_code(fn_info):
    ret = ("{0.doc}{0.rtype} {0.name} ({0.args}) {{\n" +
            "    {0.body}" +
            "}}\n\n").format(fn_info)

    return ret

def get_fn_header(fn_info):
    return "{0.doc}{0.rtype} {0.name} ({0.args});\n\n".format(fn_info)

def generate_source_header(api_file, out_dir, skip_patterns=None):
    skip_patterns = skip_patterns or list()
    file_name = os.path.basename(api_file)
    mod_name, dot, ext = file_name.partition(".")
    if not dot or ext != "api":
        print("Invalid file given, needs to be in MODNAME.api format")
        return 1

    includes, items = process_file(open(api_file, "r"))
    filtered = list()
    for item in items:
        if isinstance(item, FuncInfo):
            if not any(re.search(pattern, item.name) for pattern in skip_patterns):
                filtered.append(item)
        elif not any(re.search(pattern, item) for pattern in skip_patterns):
            filtered.append(item)
    items = filtered

    nonapi_fn_infos = [item for item in items if isinstance(item, FuncInfo) and item.body]
    api_fn_infos = [item for item in items if isinstance(item, FuncInfo) and not item.body and item.doc]
    with open(os.path.join(out_dir, mod_name + ".c"), "w") as src_f:
        for info in nonapi_fn_infos:
            src_f.write(get_fn_code(info))
        for info in api_fn_infos:
            src_f.write(get_func_boilerplate(info))
        src_f.write(get_loading_func(api_fn_infos, mod_name))
        src_f.write(get_unloading_func(api_fn_infos, mod_name))

    written_fns = set()
    with open(os.path.join(out_dir, mod_name + ".h"), "w") as hdr_f:
        hdr_f.write(get_includes_str(includes))
        for item in items:
            if isinstance(item, FuncInfo):
                if item.name not in written_fns:
                    hdr_f.write(get_fn_header(item))
                    written_fns.add(item.name)
            else:
                hdr_f.write(item)

    return 0

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Needs a file name and output directory, exiting.")
        print("Usage: %s FILE_NAME OUTPUT_DIR [SKIP_PATTERNS]" % sys.argv[0])
        sys.exit(1)

    if not os.path.exists(sys.argv[1]):
        print("Input file '%s' doesn't exist" % sys.argv[1])
        sys.exit(1)

    skip_patterns = None
    if len(sys.argv) > 3:
        skip_patterns = sys.argv[3:]

    out_dir = sys.argv[2]
    if not os.path.exists (out_dir):
        os.makedirs(out_dir)

    status = generate_source_header(sys.argv[1], out_dir, skip_patterns)

    sys.exit(status)
