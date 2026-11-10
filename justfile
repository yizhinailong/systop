project_name := "systop"
platform := os()
build_dir := "./build"
publish_dir := "./publish"
exe_suffix := if platform == "windows" { ".exe" } else { "" }

default:
    @cmake --version
    @ninja --version
    @clang-cl --version
    @just --list

configure profile="debug" *args:
    @cmake --preset "{{ platform }}-{{ profile }}" {{ args }}

build profile="debug":
    @[ -f "{{ build_dir }}/{{ platform }}-{{ profile }}/CMakeCache.txt" ] || cmake --preset "{{ platform }}-{{ profile }}"
    @cmake --build --preset "{{ platform }}-{{ profile }}"
    @cmake -E copy_if_different "{{ build_dir }}/{{ platform }}-{{ profile }}/compile_commands.json" "{{ build_dir }}/compile_commands.json"

publish profile="release": (build profile)
    @cmake --install "{{ build_dir }}/{{ platform }}-{{ profile }}" --prefix "{{ publish_dir }}"

run profile="debug": (build profile)
    @{{ build_dir }}/{{ platform }}-{{ profile }}/{{ project_name }}{{ exe_suffix }}

clean profile="":
    @cmake -E rm -rf {{ if profile == "" { build_dir + " " + publish_dir } else { build_dir + "/" + platform + "-" + profile } }}
