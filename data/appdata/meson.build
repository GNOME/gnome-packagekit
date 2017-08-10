appdata_files = [
  'org.gnome.Packages.appdata.xml',
  'org.gnome.PackageUpdater.appdata.xml'
]
foreach appdata_file: appdata_files
  i18n.merge_file(
    input: appdata_file + '.in',
    output: appdata_file,
    type: 'xml',
    po_dir: join_paths(meson.source_root(), 'po'),
    install: true,
    install_dir: join_paths(get_option('datadir'), 'metainfo')
  )
endforeach
