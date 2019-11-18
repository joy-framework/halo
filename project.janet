(declare-project
  :name "halo"
  :description "Janet bindings for civetweb"
  :author "Sean Walker"
  :license "MIT"
  :url "https://github.com/joy-framework/halo"
  :repo "git+https://github.com/joy-framework/halo.git")

(declare-native
  :name "halo"
  :embedded ["halo_lib.janet"]
  :source ["halo.c" "sds.c" "http_parser.c"])
