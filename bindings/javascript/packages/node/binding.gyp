{
    "targets": [
        {
            "target_name": "wkp_node",
            "sources": ["src/addon.cc", "../../../../core/src/core.c"],
            "include_dirs": ["<!@(node -p \"require('node-addon-api').include\")", "../../../../core/include"],
            "defines": ["NAPI_CPP_EXCEPTIONS"],
            "cflags!": ["-fno-exceptions"],
            "cflags_cc!": ["-fno-exceptions"],
            "cflags_cc": ["-std=c++17", "-fexceptions"],
            "xcode_settings": {"GCC_ENABLE_CPP_EXCEPTIONS": "YES", "CLANG_CXX_LIBRARY": "libc++"},
            "msvs_settings": {"VCCLCompilerTool": {"ExceptionHandling": 1, "AdditionalOptions": ["/std:c++17"]}},
            "conditions": [["OS=='win'", {"defines": ["_CRT_SECURE_NO_WARNINGS"]}]],
        }
    ]
}
