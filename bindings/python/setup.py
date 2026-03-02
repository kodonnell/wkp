import os
import re
import subprocess
import sys
from pathlib import Path

import nanobind
from setuptools import Extension, setup
from setuptools.command.build_ext import build_ext

ROOT = Path(__file__).resolve().parents[2]


def read_binding_version_from_pyproject() -> str:
    pyproject = Path(__file__).resolve().parent / "pyproject.toml"
    content = pyproject.read_text(encoding="utf-8")
    match = re.search(r'^version\s*=\s*"([^"]+)"', content, flags=re.MULTILINE)
    if not match:
        raise RuntimeError(f"Could not parse [project].version from {pyproject}")
    return match.group(1)


binding_version = read_binding_version_from_pyproject()


class BuildExtWithNanobindStubs(build_ext):
    def run(self):
        super().run()
        self._generate_nanobind_stubs()

    def _generate_nanobind_stubs(self):
        build_lib = Path(self.build_lib).resolve()
        out_dir = build_lib / "wkp"
        out_dir.mkdir(parents=True, exist_ok=True)
        extension_dir = out_dir

        env = os.environ.copy()
        pythonpath_entries = [
            str(extension_dir),
            str(build_lib),
            str((Path(__file__).resolve().parent / "src").resolve()),
        ]
        existing_pythonpath = env.get("PYTHONPATH")
        if existing_pythonpath:
            pythonpath_entries.append(existing_pythonpath)
        env["PYTHONPATH"] = os.pathsep.join(pythonpath_entries)

        cmd = [
            sys.executable,
            "-m",
            "nanobind.stubgen",
            "-m",
            "_core",
            "-O",
            str(out_dir),
        ]

        result = subprocess.run(cmd, env=env, check=False)
        if result.returncode != 0:
            raise RuntimeError("nanobind stub generation failed for module 'wkp._core'")

        generated_stub = out_dir / "_core.pyi"
        if not generated_stub.exists():
            raise RuntimeError(f"nanobind stub generation did not create expected file: {generated_stub}")


define_macros = [("WKP_VERSION", f'"{binding_version}"')]
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
    version=binding_version,
    ext_modules=extensions,
    cmdclass={"build_ext": BuildExtWithNanobindStubs},
)
