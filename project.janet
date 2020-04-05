(declare-project
  :name "halo"
  :description "Janet bindings for civetweb"
  :author "Sean Walker"
  :license "MIT"
  :dependencies ["https://github.com/joy-framework/tester"]
  :url "https://github.com/joy-framework/halo"
  :repo "git+https://github.com/joy-framework/halo.git")

(declare-native
  :name "halo"
  :embedded ["halo_lib.janet"]
  :source ["halo.c" "sandbird.c" "http_parser.c"])
