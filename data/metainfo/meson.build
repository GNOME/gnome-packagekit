metainfo_files = [
  'org.gnome.Packages.metainfo.xml',
  'org.gnome.PackageUpdater.metainfo.xml'
]
foreach metainfo_file: metainfo_files
  i18n.merge_file(
    input: metainfo_file + '.in',
    output: metainfo_file,
    type: 'xml',
    po_dir: join_paths(meson.source_root(), 'po'),
    install: true,
    install_dir: join_paths(get_option('datadir'), 'metainfo')
  )
endforeach
