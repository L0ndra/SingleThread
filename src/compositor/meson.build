compositor_sources = [
  'main.c',
  'server.c',
  'output.c',
  'input.c',
  'view.c',
  'task.c',
  'layout.c',
  'rules.c',
  'stw_config.c',
  'keybind.c',
  'ipc.c',
  'session.c',
  'xwayland.c',
]

compositor_deps = [
  wayland_server,
  wlroots,
  xkbcommon,
  pixman,
  libinput,
  json_c,
]

if xcb.found()
  compositor_deps += xcb
endif
if xcb_errors.found()
  compositor_deps += xcb_errors
endif

executable(
  'singlethread',
  compositor_sources + wl_protocol_sources + wl_protocol_headers,
  dependencies: compositor_deps,
  include_directories: include_directories('..', '../..'),
  install: true,
)
