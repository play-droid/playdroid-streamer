project(
  'playdroid-streamer',
  'cpp',
  version : '1.0.0',
  default_options : ['warning_level=3']
)

project_source_files = [
  'src/main.cpp',
]

project_server_source_files = [
  'src/server.cpp',
]

public_headers = include_directories('include')

depnames = [
  'gstreamer-1.0', 'gstreamer-allocators-1.0',
  'gstreamer-app-1.0', 'gstreamer-video-1.0',
  'gobject-2.0', 'glib-2.0'
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

project_server_target = executable(
  meson.project_name() + '-server',
  project_server_source_files,
  dependencies: project_dependencies,
  install : true,
  cpp_args : build_args,
  include_directories : public_headers,
)
