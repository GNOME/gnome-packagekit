docbook2man = find_program('docbook2man', required : false)
if docbook2man.found()
  custom_target('gpk-application-man',
    output : 'gpk-application.1',
    input : 'gpk-application.sgml',
    command : [docbook2man, '@INPUT@', '--output', 'man'],
    install : true,
    install_dir : 'share/man/man1'
  )
  custom_target('gpk-log-man',
    output : 'gpk-log.1',
    input : 'gpk-log.sgml',
    command : [docbook2man, '@INPUT@', '--output', 'man'],
    install : true,
    install_dir : 'share/man/man1'
  )
  custom_target('gpk-prefs-man',
    output : 'gpk-prefs.1',
    input : 'gpk-prefs.sgml',
    command : [docbook2man, '@INPUT@', '--output', 'man'],
    install : true,
    install_dir : 'share/man/man1'
  )
  custom_target('gpk-update-viewer-man',
    output : 'gpk-update-viewer.1',
    input : 'gpk-update-viewer.sgml',
    command : [docbook2man, '@INPUT@', '--output', 'man'],
    install : true,
    install_dir : 'share/man/man1'
  )
endif
