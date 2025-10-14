{
  "variables": {
    "enable_native_tuning%": 1,
    "disable_avx256%": 0,
    "disable_crcutil%": 0
  },
  "target_defaults": {
    "conditions": [
      ['target_arch=="ia32"', {
        "msvs_settings": {"VCCLCompilerTool": {"EnableEnhancedInstructionSet": "2"}}
      }],
      ['OS!="win" and enable_native_tuning!=0 and target_arch==host_arch', {
        "variables": {
          "supports_native%": "<!(<!(echo ${CXX_target:-${CXX:-c++}}) -MM -E src/common.h -march=native 2>/dev/null || true)"
        },
        "conditions": [
          ['supports_native!=""', {
            "cflags": ["-march=native"],
            "cxxflags": ["-march=native"],
            "xcode_settings": {
              "OTHER_CFLAGS": ["-march=native"],
              "OTHER_CXXFLAGS": ["-march=native"],
            },
            "defines": ["YENC_BUILD_NATIVE=1"]
          }]
        ]
      }],
      ['OS!="win" and target_arch in "ia32 x64" and enable_native_tuning==0', {
        "variables": {
          "supports_avx2_nosplit%": "<!(<!(echo ${CXX_target:-${CXX:-c++}}) -MM -E src/common.h -mavx2 -mno-avx256-split-unaligned-load -mno-avx256-split-unaligned-store 2>/dev/null || true)"
        },
        "conditions": [
          ['supports_avx2_nosplit!=""', {
            "cflags": ["-mno-avx256-split-unaligned-load", "-mno-avx256-split-unaligned-store"],
            "cxxflags": ["-mno-avx256-split-unaligned-load", "-mno-avx256-split-unaligned-store"],
            "xcode_settings": {
              "OTHER_CFLAGS": ["-mno-avx256-split-unaligned-load", "-mno-avx256-split-unaligned-store"],
              "OTHER_CXXFLAGS": ["-mno-avx256-split-unaligned-load", "-mno-avx256-split-unaligned-store"],
            }
          }]
        ]
      }],
      ['disable_avx256!=0', {
        "defines": ["YENC_DISABLE_AVX256=1"]
      }],
      ['disable_crcutil!=0', {
        "defines": ["YENC_DISABLE_CRCUTIL=1"]
      }],
      ['OS!="win"', {
        "variables": {
          "missing_memalign%": "<!(<!(echo ${CXX_target:-${CXX:-c++}}) -c src/test_alignalloc.cc -o /dev/null -Werror 2>/dev/null || echo failed)",
        },
        "conditions": [
          ['missing_memalign!=""', {
            "defines": ["_POSIX_C_SOURCE=200112L"],
          }]
        ]
      }]
    ],
    "cflags": ["-Wno-unused-function"],
    "cxxflags": ["-Wno-unused-function", "-std=c++03"],
    "xcode_settings": {
      "OTHER_CFLAGS": ["-Wno-unused-function"],
      "OTHER_CXXFLAGS": ["-Wno-unused-function"]
    },
    "msvs_settings": {"VCCLCompilerTool": {"Optimization": "MaxSpeed"}},
    "configurations": {"Release": {
      "cflags": ["-fomit-frame-pointer"],
      "cxxflags": ["-fomit-frame-pointer"],
      "xcode_settings": {
        "OTHER_CFLAGS": ["-fomit-frame-pointer"],
        "OTHER_CXXFLAGS": ["-fomit-frame-pointer"]
      }
    }}
  },
  "targets": [
    {
      "target_name": "yencode",
      "dependencies": ["yencode_sse2", "yencode_ssse3", "yencode_clmul", "yencode_clmul256", "yencode_avx", "yencode_avx2", "yencode_vbmi2", "yencode_neon", "yencode_armcrc", "yencode_pmull", "yencode_rvv", "yencode_zbkc"],
      "sources": [
        "src/yencode.cc",
        "src/platform.cc",
        "src/encoder.cc",
        "src/decoder.cc",
        "src/crc.cc"
      ],
      "conditions": [
        ['target_arch in "ia32 x64" and disable_crcutil==0', {
          "dependencies": ["crcutil"],
          "include_dirs": ["crcutil-1.0/code","crcutil-1.0/examples"]
        }]
      ]
    },
    {
      "target_name": "yencode_sse2",
      "type": "static_library",
      "sources": [
        "src/encoder_sse2.cc",
        "src/decoder_sse2.cc"
      ],
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "cxxflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "xcode_settings": {
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
        "OTHER_CXXFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch in "ia32 x64"', {
          "cflags": ["-msse2"],
          "cxxflags": ["-msse2"],
          "xcode_settings": {
            "OTHER_CFLAGS": ["-msse2"],
            "OTHER_CXXFLAGS": ["-msse2"],
          }
        }]
      ]
    },
    {
      "target_name": "yencode_ssse3",
      "type": "static_library",
      "sources": [
        "src/encoder_ssse3.cc",
        "src/decoder_ssse3.cc"
      ],
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "cxxflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "xcode_settings": {
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
        "OTHER_CXXFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
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
        "src/crc_folding.cc"
      ],
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "cxxflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "xcode_settings": {
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
        "OTHER_CXXFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
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
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "cxxflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "xcode_settings": {
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
        "OTHER_CXXFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
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
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "cxxflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "xcode_settings": {
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
        "OTHER_CXXFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch in "ia32 x64" and OS!="win"', {
          "variables": {"supports_avx2%": "<!(<!(echo ${CXX_target:-${CXX:-c++}}) -MM -E src/decoder_avx2.cc -mavx2 2>/dev/null || true)"},
          "conditions": [
            ['supports_avx2!=""', {
              "cflags": ["-mavx2", "-mpopcnt", "-mbmi", "-mbmi2", "-mlzcnt"],
              "cxxflags": ["-mavx2", "-mpopcnt", "-mbmi", "-mbmi2", "-mlzcnt"],
              "xcode_settings": {
                "OTHER_CFLAGS": ["-mavx2", "-mpopcnt", "-mbmi", "-mbmi2", "-mlzcnt"],
                "OTHER_CXXFLAGS": ["-mavx2", "-mpopcnt", "-mbmi", "-mbmi2", "-mlzcnt"],
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
      "target_name": "yencode_clmul256",
      "type": "static_library",
      "sources": [
        "src/crc_folding_256.cc"
      ],
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "cxxflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "xcode_settings": {
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
        "OTHER_CXXFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch in "ia32 x64" and OS!="win"', {
          "variables": {"supports_vpclmul%": "<!(<!(echo ${CXX_target:-${CXX:-c++}}) -MM -E src/crc_folding_256.cc -mavx2 -mvpclmulqdq 2>/dev/null || true)"},
          "conditions": [
            ['supports_vpclmul!=""', {
              "cflags": ["-mavx2", "-mvpclmulqdq", "-mpclmul"],
              "cxxflags": ["-mavx2", "-mvpclmulqdq", "-mpclmul"],
              "xcode_settings": {
                "OTHER_CFLAGS": ["-mavx2", "-mvpclmulqdq", "-mpclmul"],
                "OTHER_CXXFLAGS": ["-mavx2", "-mvpclmulqdq", "-mpclmul"],
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
      "target_name": "yencode_vbmi2",
      "type": "static_library",
      "sources": [
        "src/decoder_vbmi2.cc", "src/encoder_vbmi2.cc"
      ],
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "cxxflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "xcode_settings": {
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
        "OTHER_CXXFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch in "ia32 x64" and OS!="win"', {
          "variables": {
            "supports_vbmi2%": "<!(<!(echo ${CXX_target:-${CXX:-c++}}) -MM -E src/encoder_vbmi2.cc -mavx512vl -mavx512vbmi2 2>/dev/null || true)",
            "supports_avx10%": "<!(<!(echo ${CXX_target:-${CXX:-c++}}) -MM -E src/encoder_vbmi2.cc -mavx512vl -mno-evex512 2>/dev/null || true)"
          },
          "conditions": [
            ['supports_vbmi2!=""', {
              "cflags": ["-mavx512vbmi2", "-mavx512vl", "-mavx512bw", "-mpopcnt", "-mbmi", "-mbmi2", "-mlzcnt"],
              "cxxflags": ["-mavx512vbmi2", "-mavx512vl", "-mavx512bw", "-mpopcnt", "-mbmi", "-mbmi2", "-mlzcnt"],
              "xcode_settings": {
                "OTHER_CFLAGS": ["-mavx512vbmi2", "-mavx512vl", "-mavx512bw", "-mpopcnt", "-mbmi", "-mbmi2", "-mlzcnt"],
                "OTHER_CXXFLAGS": ["-mavx512vbmi2", "-mavx512vl", "-mavx512bw", "-mpopcnt", "-mbmi", "-mbmi2", "-mlzcnt"],
              }
            }],
            ['supports_avx10!=""', {
              "cflags": ["-mno-evex512"],
              "cxxflags": ["-mno-evex512"],
              "xcode_settings": {
                "OTHER_CFLAGS": ["-mno-evex512"],
                "OTHER_CXXFLAGS": ["-mno-evex512"],
              }
            }]
          ]
        }],
        ['target_arch in "ia32 x64" and OS=="win"', {
          "msvs_settings": {"VCCLCompilerTool": {"AdditionalOptions": ["/arch:AVX512"], "EnableEnhancedInstructionSet": "0"}}
        }]
      ]
    },
    {
      "target_name": "yencode_neon",
      "type": "static_library",
      "sources": [
        "src/encoder_neon.cc"
      ],
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "cxxflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "xcode_settings": {
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
        "OTHER_CXXFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch=="arm"', {
          "cflags": ["-mfpu=neon","-fno-lto"],
          "cxxflags": ["-mfpu=neon","-fno-lto"],
          "xcode_settings": {
            "OTHER_CFLAGS": ["-mfpu=neon","-fno-lto"],
            "OTHER_CXXFLAGS": ["-mfpu=neon","-fno-lto"],
          }
        }],
        ['target_arch=="arm64"', {
          "sources": ["src/decoder_neon64.cc"]
        }, {
          "sources": ["src/decoder_neon.cc"]
        }]
      ]
    },
    {
      "target_name": "yencode_rvv",
      "type": "static_library",
      "sources": [
        "src/encoder_rvv.cc",
        "src/decoder_rvv.cc"
      ],
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "cxxflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "xcode_settings": {
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
        "OTHER_CXXFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch=="riscv64" and OS!="win"', {
          "variables": {"supports_rvv%": "<!(<!(echo ${CXX_target:-${CXX:-c++}}) -MM -E src/encoder_rvv.cc -march=rv64gcv 2>/dev/null || true)"},
          "conditions": [
            ['supports_rvv!=""', {
              "cflags!": ["-march=native"],
              "cxxflags!": ["-march=native"],
              "cflags": ["-march=rv64gcv"],
              "cxxflags": ["-march=rv64gcv"],
              "xcode_settings": {
                "OTHER_CFLAGS!": ["-march=native"],
                "OTHER_CXXFLAGS!": ["-march=native"],
                "OTHER_CFLAGS": ["-march=rv64gcv"],
                "OTHER_CXXFLAGS": ["-march=rv64gcv"],
              }
            }]
          ]
        }],
        ['target_arch=="riscv32" and OS!="win"', {
          "variables": {"supports_rvv%": "<!(<!(echo ${CXX_target:-${CXX:-c++}}) -MM -E src/encoder_rvv.cc -march=rv32gcv 2>/dev/null || true)"},
          "conditions": [
            ['supports_rvv!=""', {
              "cflags!": ["-march=native"],
              "cxxflags!": ["-march=native"],
              "cflags": ["-march=rv32gcv"],
              "cxxflags": ["-march=rv32gcv"],
              "xcode_settings": {
                "OTHER_CFLAGS!": ["-march=native"],
                "OTHER_CXXFLAGS!": ["-march=native"],
                "OTHER_CFLAGS": ["-march=rv32gcv"],
                "OTHER_CXXFLAGS": ["-march=rv32gcv"],
              }
            }]
          ]
        }]
      ]
    },
    {
      "target_name": "yencode_armcrc",
      "type": "static_library",
      "sources": [
        "src/crc_arm.cc"
      ],
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "cxxflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "xcode_settings": {
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
        "OTHER_CXXFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
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
        }],
        ['OS!="win" and target_arch=="arm"', {
          "cflags": ["-mfpu=fp-armv8","-fno-lto"],
          "cxxflags": ["-mfpu=fp-armv8","-fno-lto"],
          "xcode_settings": {
            "OTHER_CFLAGS": ["-mfpu=fp-armv8","-fno-lto"],
            "OTHER_CXXFLAGS": ["-mfpu=fp-armv8","-fno-lto"]
          }
        }]
      ]
    },
    {
      "target_name": "yencode_pmull",
      "type": "static_library",
      "sources": [
        "src/crc_arm_pmull.cc"
      ],
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "cxxflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "xcode_settings": {
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
        "OTHER_CXXFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch in "arm arm64"', {
          "cflags!": ["-march=native"],
          "cxxflags!": ["-march=native"],
          "cflags": ["-march=armv8-a+crc+crypto"],
          "cxxflags": ["-march=armv8-a+crc+crypto"],
          "xcode_settings": {
            "OTHER_CFLAGS!": ["-march=native"],
            "OTHER_CXXFLAGS!": ["-march=native"],
            "OTHER_CFLAGS": ["-march=armv8-a+crc+crypto"],
            "OTHER_CXXFLAGS": ["-march=armv8-a+crc+crypto"],
          }
        }],
        ['OS!="win" and target_arch=="arm"', {
          "cflags": ["-mfpu=neon","-fno-lto"],
          "cxxflags": ["-mfpu=neon","-fno-lto"],
          "xcode_settings": {
            "OTHER_CFLAGS": ["-mfpu=neon","-fno-lto"],
            "OTHER_CXXFLAGS": ["-mfpu=neon","-fno-lto"]
          }
        }]
      ]
    },
    {
      "target_name": "yencode_zbkc",
      "type": "static_library",
      "sources": [
        "src/crc_riscv.cc"
      ],
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "cxxflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "xcode_settings": {
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
        "OTHER_CXXFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "conditions": [
        ['target_arch=="riscv64" and OS!="win"', {
          "variables": {"supports_zbkc%": "<!(<!(echo ${CXX_target:-${CXX:-c++}}) -MM -E src/crc_riscv.cc -march=rv64gc_zbkc 2>/dev/null || true)"},
          "conditions": [
            ['supports_zbkc!=""', {
              "cflags!": ["-march=native"],
              "cxxflags!": ["-march=native"],
              "cflags": ["-march=rv64gc_zbkc"],
              "cxxflags": ["-march=rv64gc_zbkc"],
              "xcode_settings": {
                "OTHER_CFLAGS!": ["-march=native"],
                "OTHER_CXXFLAGS!": ["-march=native"],
                "OTHER_CFLAGS": ["-march=rv64gc_zbkc"],
                "OTHER_CXXFLAGS": ["-march=rv64gc_zbkc"],
              }
            }]
          ]
        }],
        ['target_arch=="riscv32" and OS!="win"', {
          "variables": {"supports_zbkc%": "<!(<!(echo ${CXX_target:-${CXX:-c++}}) -MM -E src/crc_riscv.cc -march=rv32gc_zbkc 2>/dev/null || true)"},
          "conditions": [
            ['supports_zbkc!=""', {
              "cflags!": ["-march=native"],
              "cxxflags!": ["-march=native"],
              "cflags": ["-march=rv32gc_zbkc"],
              "cxxflags": ["-march=rv32gc_zbkc"],
              "xcode_settings": {
                "OTHER_CFLAGS!": ["-march=native"],
                "OTHER_CXXFLAGS!": ["-march=native"],
                "OTHER_CFLAGS": ["-march=rv32gc_zbkc"],
                "OTHER_CXXFLAGS": ["-march=rv32gc_zbkc"],
              }
            }]
          ]
        }]
      ]
    },
    {
      "target_name": "crcutil",
      "type": "none",
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
      "cflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "cxxflags!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
      "xcode_settings": {
        "OTHER_CFLAGS": ["-fomit-frame-pointer", "-Wno-expansion-to-defined"],
        "OTHER_CXXFLAGS": ["-fomit-frame-pointer", "-Wno-expansion-to-defined"],
        "OTHER_CFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"],
        "OTHER_CXXFLAGS!": ["-fno-omit-frame-pointer", "-fno-tree-vrp", "-fno-strict-aliasing"]
      },
      "msvs_settings": {"VCCLCompilerTool": {"BufferSecurityCheck": "false"}},
      "include_dirs": ["crcutil-1.0/code", "crcutil-1.0/tests"],
      "defines": ["CRCUTIL_USE_MM_CRC32=0"],
      "conditions": [
        ['target_arch in "ia32 x64" and disable_crcutil==0', {
          "type": "static_library",
        }]
      ]
    }
  ]
}