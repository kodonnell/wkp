import os
import toml
import sys

import setuptools_scm  # noqa: F401 - to avoid version = 0.0.0 errors if built without setuptools_scm installed
from setuptools import Extension, setup

USE_CYTHON = os.getenv("USE_CYTHON") == "1"

try:
    from Cython.Build import cythonize
    from Cython.Compiler.Version import version as cython_version
    from packaging.version import Version
except ImportError:
    cythonize = None
    if USE_CYTHON:
        raise RuntimeError("You've set USE_CYTHON=1 but don't have Cython installed!")


# https://cython.readthedocs.io/en/latest/src/userguide/source_files_and_compilation.html#distributing-cython-modules
def no_cythonize(extensions, **_ignore):
    for extension in extensions:
        sources = []
        for sfile in extension.sources:
            path, ext = os.path.splitext(sfile)
            if ext in (".pyx", ".py"):
                ext = ".c"
                sfile = path + ext
            sources.append(sfile)
        extension.sources[:] = sources
    return extensions

define_macros=[]
if (
    sys.version_info > (3, 13, 0)
    and hasattr(sys, "_is_gil_enabled")
    and not sys._is_gil_enabled()
):
    print("build nogil")
    define_macros.append(
        ("Py_GIL_DISABLED", "1"),
    )

extensions = [Extension( "wkp.encode", sources=["src/wkp/encode.pyx"],  define_macros=define_macros)]


if USE_CYTHON:
    compiler_directives = {
        "language_level": 3,
        "embedsignature": True,
        "boundscheck": False,
        "wraparound": False,
        "cdivision": True,
    }
    if Version(cython_version) >= Version("3.1.0a0"):
        compiler_directives["freethreading_compatible"] = True
    extensions = cythonize(extensions, compiler_directives=compiler_directives)
else:
    extensions = no_cythonize(extensions)


# Read dependencies from pyproject.toml
with open("pyproject.toml") as f:
    pyproject = toml.load(f)
install_requires = pyproject["project"].get("dependencies", [])
dev_requires = pyproject["project"]["optional-dependencies"]["dev"]

setup(
    ext_modules=extensions,
    install_requires=install_requires,
    extras_require={
        "dev": dev_requires,
    },
    include_dirs=[],
)
