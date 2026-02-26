import os
import re
import sys
from pathlib import Path

import nanobind
from setuptools import Extension, setup

ROOT = Path(__file__).resolve().parents[2]


def read_version_from_header() -> str:
    header = ROOT / "core" / "include" / "wkp" / "_version.h"
    content = header.read_text(encoding="utf-8")
    match = re.search(r'#define\s+WKP_CORE_VERSION\s+"([^"]+)"', content)
    if not match:
        raise RuntimeError(f"Could not parse WKP_CORE_VERSION from {header}")
    return match.group(1)


shared_version = read_version_from_header()

define_macros = [("WKP_VERSION", f'"{shared_version}"')]
if sys.version_info > (3, 13, 0) and hasattr(sys, "_is_gil_enabled") and not sys._is_gil_enabled():
    define_macros.append(("Py_GIL_DISABLED", "1"))

extensions = []

nanobind_include_dir = nanobind.include_dir()
nanobind_source_dir = nanobind.source_dir()
nanobind_ext_robin = os.path.join(os.path.dirname(nanobind_source_dir), "ext", "robin_map", "include")

nb_extra_compile_args = []
if sys.platform == "win32":
    nb_extra_compile_args.extend(["/std:c++17", "/EHsc", "/DNB_COMPACT_ASSERTIONS"])
else:
    nb_extra_compile_args.extend(
        ["-std=c++17", "-fvisibility=hidden", "-fno-strict-aliasing", "-DNB_COMPACT_ASSERTIONS"]
    )

extensions.append(
    Extension(
        "wkp._core",
        sources=[
            "src/wkp/core_nb.cpp",
            os.path.join(nanobind_source_dir, "nb_combined.cpp"),
            "../../core/src/core.cpp",
        ],
        include_dirs=[
            "../../core/include",
            nanobind_include_dir,
            nanobind_ext_robin,
        ],
        language="c++",
        extra_compile_args=nb_extra_compile_args,
        define_macros=define_macros,
    )
)

setup(
    version=shared_version,
    ext_modules=extensions,
)
