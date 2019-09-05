pkg = import('pkgconfig')
libsysbak_dir = meson.current_source_dir()

subdir = 'libsysbak'

headers = files(
  'sysbak-gdbus.h',
)

install_headers(
  headers + ['sysbak-extfs.h'],
  install_dir: join_paths(sysbak_pkgincludedir, subdir),
)

sources = files(
  'sysbak-gdbus.c',
  'sysbak-extfs.c',
)

dbus_sources = []

deps = [
  crypt_dep,
  gio_unix_dep,
  glib_dep,
  libaccounts_generated_dep,
  ext2fs_dep,
]

symbol_map = join_paths(meson.current_source_dir(), 'symbol.map')
ldflags = cc.get_supported_link_arguments('-Wl,--version-script,@0@'.format(symbol_map))

libsysbak = shared_library(
  sysbak_name,
  sources: sources + dbus_sources,
  version: libversion,
  include_directories: top_srcdir,
  dependencies: deps,
  c_args: '-DG_LOG_DOMAIN="@0@"'.format(meson.project_name()),
  link_args: ldflags,
  link_depends: symbol_map,
  install: true,
)

pkg.generate(
  libsysbak,
  name: 'Sysbak Admin',
  description: 'Client Library for communicating with group service',
  filebase: sysbak_name,
  subdirs: sysbak_api_name,
  variables: [
    'exec_prefix=${prefix}',
    'datadir=${prefix}/' + get_option('datadir'),
    'interfacesdir=${datadir}/sysbak-admin/interfaces',
  ],
)
