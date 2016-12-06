icon_sizes = ['16x16', '22x22', '24x24', '48x48', 'scalable']
foreach size: icon_sizes
  install_subdir(size + '/actions',
    install_dir: join_paths(
      get_option('datadir'),
      'gnome-packagekit',
      'icons',
      'hicolor',
      size
    )
  )
  install_subdir(size + '/status',
    install_dir: join_paths(
      get_option('datadir'),
      'gnome-packagekit',
      'icons',
      'hicolor',
      size
    )
  )
endforeach

meson.add_install_script('create-application-icons.sh')
