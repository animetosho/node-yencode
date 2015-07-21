{
  "targets": [
    {
      "target_name": "yencode",
      "sources": ["yencode.cc"],
      "variables": {
        "node_version": '<!((if [ -n `which nodejs` ]; then nodejs --version; else node --version; fi) | sed -e "s/^v\([0-9]*\\.[0-9]*\).*$/\\1/")',
      },
      "conditions": [
        [ "node_version == '0.10'", { "defines": ["NODE_010"] } ]
      ]
    }
  ]
}