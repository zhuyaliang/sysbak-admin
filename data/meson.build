ifaces = files(
  sysbak_namespace + '.xml',
)

install_data(
  ifaces,
  install_dir: dbus_ifaces_dir,
)

install_data(
  sysbak_namespace + '.conf',
  install_dir: dbus_conf_dir,
)

service_conf = configuration_data()
service_conf.set('libexecdir', sysbak_libexecdir)

service = sysbak_namespace + '.service'

configure_file(
  input: service + '.in',
  output: service,
  configuration: service_conf,
  install: true,
  install_dir: dbus_sys_dir,
)

if install_systemd_unit_dir
  service = 'sysbak-admin-daemon.service'

  configure_file(
    input: service + '.in',
    output: service,
    configuration: service_conf,
    install: true,
    install_dir: systemd_system_unit_dir,
  )
endif
