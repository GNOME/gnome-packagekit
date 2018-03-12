cargs = ['-DG_LOG_DOMAIN="GnomePackageKit"']

gpk_application_resources = gnome.compile_resources(
  'gpk-application-resources',
  'gpk-application.gresource.xml',
  source_dir : '.',
  c_name : 'gpk'
)

gpk_log_resources = gnome.compile_resources(
  'gpk-log-resources',
  'gpk-log.gresource.xml',
  source_dir : '.',
  c_name : 'gpk'
)

gpk_prefs_resources = gnome.compile_resources(
  'gpk-prefs-resources',
  'gpk-prefs.gresource.xml',
  source_dir : '.',
  c_name : 'gpk'
)

gpk_update_viewer_resources = gnome.compile_resources(
  'gpk-update-viewer-resources',
  'gpk-update-viewer.gresource.xml',
  source_dir : '.',
  c_name : 'gpk'
)

shared_srcs = [
  'gpk-debug.c',
  'gpk-enum.c',
  'gpk-dialog.c',
  'gpk-common.c',
  'gpk-task.c',
  'gpk-error.c',
]

executable(
  'gpk-application',
  gpk_application_resources,
  sources : [
    'gpk-application.c',
    shared_srcs
  ],
  include_directories : [
    include_directories('..'),
  ],
  dependencies : [
    packagekit,
    gio,
    gtk
  ],
  c_args : cargs,
  install : true,
  install_dir : 'bin'
)

executable(
  'gpk-prefs',
  gpk_prefs_resources,
  sources : [
    'gpk-prefs.c',
    shared_srcs
  ],
  include_directories : [
    include_directories('..'),
  ],
  dependencies : [
    packagekit,
    gio,
    gtk
  ],
  c_args : cargs,
  install : true,
  install_dir : 'bin'
)

executable(
  'gpk-log',
  gpk_log_resources,
  sources : [
    'gpk-log.c',
    shared_srcs
  ],
  include_directories : [
    include_directories('..'),
  ],
  dependencies : [
    packagekit,
    gio,
    gtk
  ],
  c_args : cargs,
  install : true,
  install_dir : 'bin'
)

gpk_update_viewer_srcs = [
  'gpk-update-viewer.c',
  'gpk-cell-renderer-size.c',
  'gpk-cell-renderer-info.c',
  'gpk-cell-renderer-restart.c',
  shared_srcs
]

gpk_update_viewer_deps = [
    packagekit,
    gio,
    gtk
]

if get_option('systemd')
  gpk_update_viewer_srcs += 'systemd-proxy.c'
  gpk_update_viewer_deps += [systemd, polkit]
endif

executable(
  'gpk-update-viewer',
  gpk_update_viewer_resources,
  sources : gpk_update_viewer_srcs,
  include_directories : [
    include_directories('..'),
  ],
  dependencies : gpk_update_viewer_deps,
  c_args : cargs,
  install : true,
  install_dir : 'bin'
)

if get_option('tests')
  e = executable(
    'gpk-self-test',
    sources : [
      'gpk-self-test.c',
      shared_srcs
    ],
    include_directories : [
      include_directories('..'),
    ],
    dependencies : [
      packagekit,
      gio,
      gtk
    ],
    c_args : cargs
  )
  test('gnome-packagekit-self-test', e)
endif
