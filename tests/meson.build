test_server_source_files = [
  'server.cpp',
]

public_headers = include_directories('../include')

depnames = [
  'egl', 'gl',
]

test_server_dependencies = []
foreach depname : depnames
  dep = dependency(depname, required: false)
  if not dep.found()
    error('Remoting plugin requires @0@ which was not found. '.format(depname) + user_hint)
  endif
  test_server_dependencies += dep
endforeach

# ===================================================================

test_server_server_target = executable(
  'test_server',
  test_server_source_files,
  dependencies: test_server_dependencies,
  install : true,
  include_directories : public_headers,
)
