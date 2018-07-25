{
  "targets": [
    {
      "target_name": "yencode",
      "dependencies": ["crcutil"],
      "sources": [
        "src/yencode.cc",
        "src/encoder.cc", "src/encoder_ssse3.cc", "src/encoder_neon.cc",
        "src/decoder.cc", "src/decoder_sse2.cc", "src/decoder_ssse3.cc", "src/decoder_neon.cc",
        "src/crc.cc", "src/crc_arm.cc", "src/crc_folding.c"
      ],
      "conditions": [
        ['OS=="win"', {
          "msvs_settings": {"VCCLCompilerTool": {"EnableEnhancedInstructionSet": "2"}}
        }, {
          "cflags": ["-march=native", "-O3"],
          "cxxflags": ["-march=native", "-O3"]
        }],
        ['OS in "linux android" and target_arch in "arm arm64"', {
          "variables": {
            "has_neon%": "<!(grep -e ' neon ' /proc/cpuinfo || true)",
            "has_crc%": "<!(grep -e ' crc32 ' /proc/cpuinfo || true)",
            "has_cpuid%": "<!(grep -e ' cpuid' /proc/cpuinfo || true)"
          },
          "conditions": [
            ['has_cpuid=="" and has_neon!=""', {
              "cflags": ["-mfpu=neon"],
              "cxxflags": ["-mfpu=neon"]
            }],
            ['has_cpuid=="" and has_crc!=""', {
              "cflags!": ["-march=native"],
              "cxxflags!": ["-march=native"],
              "cflags": ["-march=armv8-a+crc"],
              "cxxflags": ["-march=armv8-a+crc"]
            }]
          ]
        }],
        ['OS=="mac"', {
          "xcode_settings": {
            "OTHER_CFLAGS": ["-march=native", "-O3"],
            "OTHER_CXXFLAGS": ["-march=native", "-O3"]
          }
        }]
      ],
      "include_dirs": ["crcutil-1.0/code","crcutil-1.0/examples"]
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
      "conditions": [
        ['OS=="win"', {
          "msvs_settings": {"VCCLCompilerTool": {"EnableEnhancedInstructionSet": "2"}}
        }, {
          "cflags": ["-march=native", "-O3", "-fomit-frame-pointer", "-Wno-expansion-to-defined"],
          "cxxflags": ["-march=native", "-O3", "-fomit-frame-pointer", "-Wno-expansion-to-defined"]
        }],
        ['OS=="mac"', {
          "xcode_settings": {
            "OTHER_CFLAGS": ["-march=native", "-O3", "-fomit-frame-pointer", "-Wno-expansion-to-defined"],
            "OTHER_CXXFLAGS": ["-march=native", "-O3", "-fomit-frame-pointer", "-Wno-expansion-to-defined"]
          }
        }]
      ],
      "include_dirs": ["crcutil-1.0/code", "crcutil-1.0/tests"],
      "defines": ["CRCUTIL_USE_MM_CRC32=0"]
    }
  ]
}