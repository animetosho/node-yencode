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
          "conditons": [
            ['target_arch=="ia32" or target_arch=="x64"', {
              "cflags": ["-march=native", "-mssse3", "-mpclmul", "-msse4.1"]
            }, {
              "cflags": ["-march=native"]
            }]
          ]
        }]
      ],
      "include_dirs": ["crcutil-1.0/code"]
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
          "cflags": ["-march=native", "-fomit-frame-pointer"]
        }]
      ],
      "include_dirs": ["crcutil-1.0/code", "crcutil-1.0/tests"],
      "defines": ["CRCUTIL_USE_MM_CRC32=0"]
    }
  ]
}