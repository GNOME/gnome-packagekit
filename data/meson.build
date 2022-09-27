subdir('metainfo')
subdir('icons')

compiled = gnome.compile_schemas()
install_data('org.gnome.packagekit.gschema.xml',
  install_dir : 'share/glib-2.0/schemas'
)

install_data('org.gnome.packagekit.gschema.migrate',
  install_dir : 'share/GConf/gsettings'
)

desktop_files = [
  'gpk-log.desktop',
  'gpk-prefs.desktop',
  'org.gnome.Packages.desktop',
  'org.gnome.PackageUpdater.desktop'
]
foreach desktop_file: desktop_files
  i18n.merge_file(
    input: desktop_file + '.in',
    output: desktop_file,
    type: 'desktop',
    po_dir: join_paths(meson.source_root(), 'po'),
    install: true,
    install_dir: join_paths(get_option('datadir'), 'applications')
  )
endforeach
