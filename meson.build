project(
  'playdroid-streamer',
  'cpp', 'c',
  version : '1.0.0',
  default_options : ['warning_level=3']
)

project_source_files = [
  'src/main.cpp',
]

public_headers = include_directories('include')

depnames = [
  'wayland-client', 'libdrm',

  'gstreamer-1.0', 'gstreamer-allocators-1.0',
  'gstreamer-app-1.0', 'gstreamer-video-1.0',
  'gobject-2.0', 'glib-2.0', 'threads'
]

project_dependencies = []
foreach depname : depnames
  dep = dependency(depname, required: false)
  if not dep.found()
    error('Remoting plugin requires @0@ which was not found. '.format(depname) + user_hint)
  endif
  project_dependencies += dep
endforeach

build_args = [
]

wayland_protocols = dependency('wayland-protocols', version: '>=1.20')
wayland_scanner = find_program('wayland-scanner')
protocols_dir = wayland_protocols.get_pkgconfig_variable('pkgdatadir')

dmabuf_xml_spec = join_paths(protocols_dir, 'unstable', 'linux-dmabuf', 'linux-dmabuf-unstable-v1.xml')
dmabuf_header = custom_target('linux-dmabuf-client-header',
      command: [ wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@' ],
      input: dmabuf_xml_spec,
      output: 'linux-dmabuf-unstable-v1-client-protocol.h')
dmabuf_code = custom_target('linux-dmabuf-client-code',
      command: [ wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@' ],
      input: dmabuf_xml_spec,
      output: 'linux-dmabuf-unstable-v1-client-protocol.c')

xdg_shell_xml_spec = join_paths(protocols_dir, 'stable', 'xdg-shell', 'xdg-shell.xml')
xdg_shell_header = custom_target('linux-xdg-shell-client-header',
      command: [ wayland_scanner, 'client-header', '@INPUT@', '@OUTPUT@' ],
      input: xdg_shell_xml_spec,
      output: 'xdg-shell-client-protocol.h')
xdg_shell_code = custom_target('linux-xdg-shell-client-code',
      command: [ wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@' ],
      input: xdg_shell_xml_spec,
      output: 'xdg-shell-client-protocol.c')

way_project_source_files = ['src/wayland-window.cpp']
way_project_source_files += [ dmabuf_header, dmabuf_code, xdg_shell_header, xdg_shell_code ]
project_dependencies += wayland_protocols

project_source_files += way_project_source_files

# ===================================================================

build_args += [
  '-DPROJECT_NAME=' + meson.project_name(),
  '-DPROJECT_VERSION=' + meson.project_version(),
]

project_target = executable(
  meson.project_name(),
  project_source_files,
  dependencies: project_dependencies,
  install : true,
  cpp_args : build_args,
  include_directories : public_headers,
)

subdir('tests')
