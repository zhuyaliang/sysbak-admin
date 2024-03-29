gnome = import('gnome')
sources = []

gdbus_headers = []

ifaces = [
  ['sysbak-admin-generated',        'org.sysbak.admin', '.gdbus'],
]

foreach iface: ifaces
  gdbus_sources = gnome.gdbus_codegen(
    iface[0],
    join_paths(data_dir, iface[1] + iface[2] + '.xml'),
    interface_prefix: iface[1],
    namespace: 'Sysbak',
  )
  sources += gdbus_sources
  gdbus_headers += gdbus_sources[1]
endforeach

deps = [
  gio_dep,
  gio_unix_dep,
]

libaccounts_generated = static_library(
  'sysbak-admin-generated',
  sources: sources,
  include_directories: top_srcdir,
  dependencies: deps,
)

libaccounts_generated_dep = declare_dependency(
  sources: gdbus_headers,
  include_directories: include_directories('.'),
  dependencies: gio_dep,
  link_with: libaccounts_generated,
)

sources = files(
  'gdbus-server.c',
  'checksum.c',  
  'gdbus-extfs.c',
  'gdbus-share.c',
  'progress.c',
  'gdbus-fatfs.c',
  'gdbus-btrfs.c',
  'gdbus-disk.c',
  'btrfs/ctree.c',
  'btrfs/crc32c.c',
  'btrfs/disk-io.c',
  'btrfs/extent_io.c',
  'btrfs/extent-tree.c',
  'btrfs/extent-cache.c',
  'btrfs/file-item.c',
  'btrfs/raid6.c',
  'btrfs/rbtree-utils.c',
  'btrfs/root-tree.c',
  'btrfs/utils.c',
  'btrfs/volumes.c',
  'gdbus-xfsfs.c',
  'xfs/xfs_init.c',
  'xfs/cache.c',
  'xfs/defer_item.c',
  'xfs/xfs_linux.c',
  'xfs/rdwr.c',
  'xfs/xfs_sb.c',
  'xfs/radix-tree.c',
  'xfs/xfs_da_format.c',
)

deps = [
  gio_unix_dep,
  glib_dep,
  libaccounts_generated_dep,
  polkit_gobject_dep,
  ext2fs_dep,
  blkid_dep,
  uuid_dep,
  btrfs_dep,
  pth_dep,
  lvmapp_dep
]

executable(
  'sysbak-admin-daemon',
  sources,
  include_directories: top_srcdir,
  dependencies: deps,
  install: true,
  install_dir: sysbak_libexecdir,
)

subdir('libsysbak')
