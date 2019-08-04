{
  "target_defaults": {
    "conditions": [
      ['target_arch=="ia32"', {
        "msvs_settings": {"VCCLCompilerTool": {"EnableEnhancedInstructionSet": "2"}}
      }]
    ],
    "defines": ["YENC_ENABLE_AVX256=0"],
    "cflags": ["-march=native", "-Wno-unused-function"],
    "cxxflags": ["-march=native", "-Wno-unused-function"],
    "xcode_settings": {
      "OTHER_CFLAGS": ["-march=native", "-Wno-unused-function"],
      "OTHER_CXXFLAGS": ["-march=native", "-Wno-unused-function"]
    }
  },
  "targets": [
    {
      "target_name": "yencode",
      "dependencies": ["crcutil", "yencode_ssse3", "yencode_clmul", "yencode_avx", "yencode_avx2", "yencode_avx3", "yencode_neon", "yencode_armcrc"],
      "sources": [
        "src/yencode.cc",
        "src/encoder.cc", "src/encoder_sse2.cc",
        "src/decoder.cc", "src/decoder_sse2.cc",
        "src/crc.cc"
      ],
      "include_dirs": ["crcutil-1.0/code","crcutil-1.0/examples"]
    },
    {
      "target_name": "yencode_ssse3",
      "type": "static_library",
      "sources": [
        "src/encoder_ssse3.cc",
        "src/decoder_ssse3.cc"
      ],
      "conditions": [
        ['target_arch in "ia32 x64"', {
          "cflags": ["-mssse3"],
          "cxxflags": ["-mssse3"],
          "xcode_settings": {
            "OTHER_CFLAGS": ["-mssse3"],
            "OTHER_CXXFLAGS": ["-mssse3"],
          }
        }]
      ]
    },
    {
      "target_name": "yencode_clmul",
      "type": "static_library",
      "sources": [
        "src/crc_folding.c"
      ],
      "conditions": [
        ['target_arch in "ia32 x64"', {
          "cflags": ["-mssse3", "-msse4.1", "-mpclmul"],
          "cxxflags": ["-mssse3", "-msse4.1", "-mpclmul"],
          "xcode_settings": {
            "OTHER_CFLAGS": ["-mssse3", "-msse4.1", "-mpclmul"],
            "OTHER_CXXFLAGS": ["-mssse3", "-msse4.1", "-mpclmul"],
          }
        }]
      ]
    },
    {
      "target_name": "yencode_avx",
      "type": "static_library",
      "sources": [
        "src/encoder_avx.cc",
        "src/decoder_avx.cc"
      ],
      "conditions": [
        ['target_arch in "ia32 x64"', {
          "msvs_settings": {"VCCLCompilerTool": {"EnableEnhancedInstructionSet": "3"}},
          "cflags": ["-mavx", "-mpopcnt"],
          "cxxflags": ["-mavx", "-mpopcnt"],
          "xcode_settings": {
            "OTHER_CFLAGS": ["-mavx", "-mpopcnt"],
            "OTHER_CXXFLAGS": ["-mavx", "-mpopcnt"],
          }
        }]
      ]
    },
    {
      "target_name": "yencode_avx2",
      "type": "static_library",
      "sources": [
        "src/decoder_avx2.cc", "src/encoder_avx2.cc"
      ],
      "conditions": [
        ['target_arch in "ia32 x64" and OS!="win"', {
          "variables": {"supports_avx2%": "<!(<!(echo ${CXX_target:-${CXX:-c++}}) -MM -E src/decoder_avx2.cc -mavx2 2>/dev/null || true)"},
          "conditions": [
            ['supports_avx2!=""', {
              "cflags": ["-mavx2", "-mpopcnt", "-mbmi", "-mbmi2"],
              "cxxflags": ["-mavx2", "-mpopcnt", "-mbmi", "-mbmi2"],
              "xcode_settings": {
                "OTHER_CFLAGS": ["-mavx2", "-mpopcnt", "-mbmi", "-mbmi2"],
                "OTHER_CXXFLAGS": ["-mavx2", "-mpopcnt", "-mbmi", "-mbmi2"],
              }
            }]
          ]
        }],
        ['target_arch in "ia32 x64" and OS=="win"', {
          "msvs_settings": {"VCCLCompilerTool": {"EnableEnhancedInstructionSet": "3"}}
        }]
      ]
    },
    {
      "target_name": "yencode_avx3",
      "type": "static_library",
      "sources": [
        "src/decoder_avx3.cc"
      ],
      "conditions": [
        ['target_arch in "ia32 x64" and OS!="win"', {
          "variables": {"supports_avx3%": "<!(<!(echo ${CXX_target:-${CXX:-c++}}) -MM -E src/decoder_avx3.cc -mavx512vl -mavx512bw 2>/dev/null || true)"},
          "conditions": [
            ['supports_avx3!=""', {
              "cflags": ["-mavx512vl", "-mavx512bw", "-mbmi", "-mbmi2"],
              "cxxflags": ["-mavx512vl", "-mavx512bw", "-mbmi", "-mbmi2"],
              "xcode_settings": {
                "OTHER_CFLAGS": ["-mavx512vl", "-mavx512bw", "-mbmi", "-mbmi2"],
                "OTHER_CXXFLAGS": ["-mavx512vl", "-mavx512bw", "-mbmi", "-mbmi2"],
              }
            }]
          ]
        }],
        ['target_arch in "ia32 x64" and OS=="win"', {
          "msvs_settings": {
            "VCCLCompilerTool": {"AdditionalOptions": ["/arch:AVX512"], "EnableEnhancedInstructionSet": "0"}
          }
        }]
      ]
    },
    {
      "target_name": "yencode_neon",
      "type": "static_library",
      "sources": [
        "src/encoder_neon.cc",
        "src/decoder_neon.cc"
      ],
      "conditions": [
        ['target_arch=="arm"', {
          "cflags": ["-mfpu=neon"],
          "cxxflags": ["-mfpu=neon"],
          "xcode_settings": {
            "OTHER_CFLAGS": ["-mfpu=neon"],
            "OTHER_CXXFLAGS": ["-mfpu=neon"],
          }
        }]
      ]
    },
    {
      "target_name": "yencode_armcrc",
      "type": "static_library",
      "sources": [
        "src/crc_arm.cc"
      ],
      "conditions": [
        ['target_arch in "arm arm64"', {
          "cflags!": ["-march=native"],
          "cxxflags!": ["-march=native"],
          "cflags": ["-march=armv8-a+crc"],
          "cxxflags": ["-march=armv8-a+crc"],
          "xcode_settings": {
            "OTHER_CFLAGS!": ["-march=native"],
            "OTHER_CXXFLAGS!": ["-march=native"],
            "OTHER_CFLAGS": ["-march=armv8-a+crc"],
            "OTHER_CXXFLAGS": ["-march=armv8-a+crc"],
          }
        }]
      ]
    },
    {
      "target_name": "crcutil",
      "type": "static_library",
      "sources": [
        "crcutil-1.0/code/crc32c_sse4.cc",
        "crcutil-1.0/code/multiword_64_64_cl_i386_mmx.cc",
        "crcutil-1.0/code/multiword_64_64_gcc_amd64_asm.cc",
        "crcutil-1.0/code/multiword_64_64_gcc_i386_mmx.cc",
        "crcutil-1.0/code/multiword_64_64_intrinsic_i386_mmx.cc",
        "crcutil-1.0/code/multiword_128_64_gcc_amd64_sse2.cc",
        "crcutil-1.0/examples/interface.cc"
      ],
      "cflags": ["-fomit-frame-pointer", "-Wno-expansion-to-defined"],
      "cxxflags": ["-fomit-frame-pointer", "-Wno-expansion-to-defined"],
      "xcode_settings": {
        "OTHER_CFLAGS": ["-fomit-frame-pointer", "-Wno-expansion-to-defined"],
        "OTHER_CXXFLAGS": ["-fomit-frame-pointer", "-Wno-expansion-to-defined"]
      },
      "include_dirs": ["crcutil-1.0/code", "crcutil-1.0/tests"],
      "defines": ["CRCUTIL_USE_MM_CRC32=0"]
    }
  ]
}