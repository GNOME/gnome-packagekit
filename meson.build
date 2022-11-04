# Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
#
# Licensed under the GNU General Public License Version 2 or later
project('gnome-packagekit', 'c',
  version : '43.1',
  default_options : ['warning_level=1'],
  meson_version : '>=0.46.0'
)

conf = configuration_data()
conf.set_quoted('PACKAGE_VERSION', meson.project_version())
conf.set_quoted('VERSION', meson.project_version())

if get_option('small-form-factor')
  conf.set('PK_BUILD_SMALL_FORM_FACTOR', '1')
endif

# get suported warning flags
test_args = [
  '-Waggregate-return',
  '-Warray-bounds',
  '-Wcast-align',
  '-Wclobbered',
  '-Wdeclaration-after-statement',
  '-Wempty-body',
  '-Wextra',
  '-Wformat=2',
  '-Wformat-nonliteral',
  '-Wformat-security',
  '-Wignored-qualifiers',
  '-Wimplicit-function-declaration',
  '-Winit-self',
  '-Winline',
  '-Wmissing-declarations',
  '-Wmissing-format-attribute',
  '-Wmissing-include-dirs',
  '-Wmissing-noreturn',
  '-Wmissing-parameter-type',
  '-Wmissing-prototypes',
  '-Wnested-externs',
  '-Wno-discarded-qualifiers',
  '-Wno-missing-field-initializers',
  '-Wno-strict-aliasing',
  '-Wno-suggest-attribute=format',
  '-Wno-unused-parameter',
  '-Wold-style-definition',
  '-Woverride-init',
  '-Wpacked',
  '-Wpointer-arith',
  '-Wredundant-decls',
  '-Wreturn-type',
  '-Wshadow',
  '-Wstrict-aliasing',
  '-Wstrict-prototypes',
  '-Wswitch-default',
  '-Wtype-limits',
  '-Wundef',
  '-Wuninitialized',
  '-Wunused-but-set-variable',
  '-Wwrite-strings'
]
cc = meson.get_compiler('c')
foreach arg: test_args
  if cc.has_argument(arg)
    add_project_arguments(arg, language : 'c')
  endif
endforeach

# enable full RELRO where possible
# FIXME: until https://github.com/mesonbuild/meson/issues/1140 is fixed
global_link_args = []
test_link_args = [
  '-Wl,-z,relro',
  '-Wl,-z,now',
]
foreach arg: test_link_args
  if cc.has_link_argument(arg)
    add_project_link_arguments(arg, language : 'c')
  endif
endforeach

gio = dependency('gio-2.0', version : '>= 2.56')
gtk = dependency('gtk+-3.0', version : '>= 3.24')
packagekit = dependency('packagekit-glib2', version : '>= 0.9.1')
libm = cc.find_library('libm', required: false)

if get_option('systemd')
  systemd = dependency('libsystemd')
  conf.set('HAVE_SYSTEMD', 1)
  polkit = dependency('polkit-gobject-1')
endif

add_project_arguments('-DI_KNOW_THE_PACKAGEKIT_GLIB2_API_IS_SUBJECT_TO_CHANGE',
                      language : 'c')

gnome = import('gnome')
i18n = import('i18n')

conf.set_quoted('GETTEXT_PACKAGE', meson.project_name())
conf.set_quoted('LOCALEDIR',
                join_paths(get_option('prefix'),
                           get_option('localedir')))
conf.set_quoted('DATADIR',
                join_paths(get_option('prefix'),
                           get_option('datadir')))
conf.set_quoted('BINDIR',
                join_paths(get_option('prefix'),
                           get_option('bindir')))
conf.set_quoted('PKGDATADIR',
                join_paths(get_option('prefix'),
                           get_option('datadir'),
                           'gnome-packagekit'))
configure_file(
  output : 'config.h',
  configuration : conf
)

subdir('man')
subdir('src')
subdir('po')
subdir('data')

if meson.version().version_compare('<0.41.0')
  archiver = find_program('git', required : false)
  if archiver.found()
    run_target('dist',
      # git config tar.tar.xz.command "xz -c"
      command: [
        'git', 'archive',
        '--prefix=' + meson.project_name() + '-' + meson.project_version() + '/',
        'HEAD',
        '--format=tar.xz',
        '--output',
        meson.project_name() + '-' + meson.project_version() + '.tar.xz'
      ]
    )
  else
    message('git not found, you will not be able to run `ninja dist`')
  endif
endif

ascli_exe = find_program('appstreamcli', required: false)
if ascli_exe.found()
  custom_target('NEWS',
    output : 'NEWS',
    input : 'data/metainfo/org.gnome.Packages.metainfo.xml.in',
    command : [ascli_exe, 'metainfo-to-news',
               '--format=text',
               '@INPUT@',
               '-'],
    capture : true
  )
endif

# FIXME: remove when https://github.com/mesonbuild/meson/issues/837 fixed
meson.add_install_script('meson_post_install.sh')
