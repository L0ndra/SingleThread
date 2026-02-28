project(
  'singlethread',
  'c',
  version: '0.1.0',
  license: 'MIT',
  meson_version: '>=0.60.0',
  default_options: [
    'c_std=c11',
    'warning_level=2',
    'werror=false',
    'b_ndebug=if-release',
  ],
)

cc = meson.get_compiler('c')

add_project_arguments(
  cc.get_supported_arguments(
    '-Wno-unused-parameter',
    '-Wundef',
    '-Wvla',
  ),
  language: 'c',
)

# Core dependencies
wayland_server = dependency('wayland-server', version: '>=1.22.0')
wayland_client = dependency('wayland-client', version: '>=1.22.0')
wayland_protos = dependency('wayland-protocols', version: '>=1.32')
wlroots = dependency('wlroots-0.18', version: '>=0.18.0', required: false)
if not wlroots.found()
  wlroots = dependency('wlroots', version: ['>=0.18.0', '<0.19.0'])
endif
xkbcommon = dependency('xkbcommon')
pixman = dependency('pixman-1')
libinput = dependency('libinput', version: '>=1.21.0')
json_c = dependency('json-c', version: '>=0.15')

# Optional dependencies
xcb = dependency('xcb', required: get_option('xwayland'))
xcb_errors = dependency('xcb-errors', required: false)

# GTK4 for shell components
gtk4 = dependency('gtk4', version: '>=4.10.0', required: get_option('shell'))
gtk4_layer_shell = dependency('gtk4-layer-shell-0', required: get_option('shell'))

# Notification support
libnotify = dependency('libnotify', required: false)

# TOML parsing
# We bundle a small TOML parser (toml.c) for minimal dependencies

conf_data = configuration_data()
conf_data.set_quoted('STW_VERSION', meson.project_version())
conf_data.set_quoted('STW_SYSCONFDIR', get_option('prefix') / get_option('sysconfdir'))
conf_data.set_quoted('STW_DATADIR', get_option('prefix') / get_option('datadir'))
conf_data.set10('STW_HAS_XWAYLAND', xcb.found())
conf_data.set10('STW_HAS_SHELL', gtk4.found() and gtk4_layer_shell.found())

configure_file(
  output: 'config.h',
  configuration: conf_data,
)

# Wayland protocol generation
wayland_scanner = find_program('wayland-scanner')
wayland_protos_dir = wayland_protos.get_variable('pkgdatadir')

wl_protocol_sources = []
wl_protocol_headers = []

protocols = [
  wayland_protos_dir / 'stable/xdg-shell/xdg-shell.xml',
  wayland_protos_dir / 'unstable/xdg-decoration/xdg-decoration-unstable-v1.xml',
  wayland_protos_dir / 'staging/cursor-shape/cursor-shape-v1.xml',
]

foreach proto : protocols
  name = proto.split('/')[-1].split('.')[0]

  wl_protocol_sources += custom_target(
    name + '-protocol.c',
    input: proto,
    output: '@BASENAME@-protocol.c',
    command: [wayland_scanner, 'private-code', '@INPUT@', '@OUTPUT@'],
  )

  wl_protocol_headers += custom_target(
    name + '-protocol.h',
    input: proto,
    output: '@BASENAME@-protocol.h',
    command: [wayland_scanner, 'server-header', '@INPUT@', '@OUTPUT@'],
  )
endforeach

# Subdirectories
subdir('src/compositor')
subdir('src/cli')

if get_option('shell')
  subdir('src/shell/panel')
  subdir('src/shell/launcher')
  subdir('src/shell/switcher')
  subdir('src/shell/notifications')
endif

# Install data files
install_data(
  'data/singlethread.desktop',
  install_dir: get_option('datadir') / 'wayland-sessions',
)

install_data(
  'config/singlethread.toml.example',
  install_dir: get_option('datadir') / 'doc' / 'singlethread',
)
