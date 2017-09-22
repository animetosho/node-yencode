{
  "targets": [
    {
      "target_name": "yencode",
      "dependencies": ["crcutil"],
      "sources": ["yencode.cc"],
      "conditions": [
        ['OS=="win"', {
          "msvs_settings": {"VCCLCompilerTool": {"EnableEnhancedInstructionSet": "2"}}
        }, {
          "cflags": ["-march=native", "-O2"],
          "cxxflags": ["-march=native", "-O2"]
        }],
        ['OS=="mac"', {
          "xcode_settings": {
            "OTHER_CFLAGS": ["-march=native", "-O2"],
            "OTHER_CXXFLAGS": ["-march=native", "-O2"]
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
          "cflags": ["-march=native", "-O3", "-fomit-frame-pointer"],
          "cxxflags": ["-march=native", "-O3", "-fomit-frame-pointer"]
        }],
        ['OS=="mac"', {
          "xcode_settings": {
            "OTHER_CFLAGS": ["-march=native", "-O3", "-fomit-frame-pointer"],
            "OTHER_CXXFLAGS": ["-march=native", "-O3", "-fomit-frame-pointer"]
          }
        }]
      ],
      "include_dirs": ["crcutil-1.0/code", "crcutil-1.0/tests"],
      "defines": ["CRCUTIL_USE_MM_CRC32=0"]
    }
  ]
}