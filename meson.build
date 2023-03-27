# Follow: https://github.com/tiernemi/meson-sample-project
    
project('p4db', 'cpp',
    version: '1.0',
    meson_version : '>= 0.55.0',
    default_options: ['warning_level=3', 'buildtype=release', 'b_lto=true', 'cpp_std=c++2a'],
    # default_options: ['warning_level=3', 'buildtype=debugoptimized', 'cpp_std=c++2a'],
    # default_options: ['warning_level=3', 'buildtype=debug', 'b_sanitize=address,undefined', 'cpp_std=c++2a'],
)
add_global_arguments('-march=native', language : ['cpp', 'c']) #vs add_global_link_arguments

add_global_arguments('-I/u/mihirs/.local/include/', language : ['cpp', 'c'])
add_global_arguments('-L/u/mihirs/.local/lib/', language : ['cpp', 'c'])
add_global_arguments('-Wl,-rpath=/u/mihirs/.local/lib/ -ltbb -latomic', language : ['cpp', 'c'])
add_global_arguments('-I/u/mihirs/.local/usr/local/include', language : ['cpp', 'c'])

project_headers = []
project_sources = []
project_includes = []
project_libs = []
project_deps = []

threads_dep = dependency('threads')

fmt_proj = subproject('fmt') #default_options: ['default_library=static'])
fmt_dep = fmt_proj.get_variable('fmt_dep')

cxxopts_proj = subproject('cxxopts') #default_options: ['default_library=static'])
cxxopts_dep = cxxopts_proj.get_variable('cxxopts_dep')

#dpdk_dep = dependency('libdpdk', required: true)
#tbb_dep = dependency('tbb', required: true)

cxx = meson.get_compiler('cpp')

project_includes += [
    include_directories('src'),
    fmt_proj.get_variable('fmt_inc'),
]

project_deps += [
    threads_dep,
    fmt_dep,
    cxxopts_dep,
    #dpdk_dep,
	# tbb_dep,
    # vtune_dep,
    # dl_dep
]

subdir('src')

p4db_bin = executable('p4db', 
    project_sources, 
    include_directories : project_includes, 
    dependencies : project_deps,
    link_with : project_libs,
    link_args : ['-L/u/mihirs/.local/lib/', '-Wl,-rpath=/u/mihirs/.local/lib/', '-ltbb', '-latomic']
)

