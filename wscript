#! /usr/bin/env python
# encoding: utf-8
#
# Copyright (C) 2007 Daniel G. Siegel <dgsiegel@gmail.com>
#
# Licensed under the GNU General Public License Version 2
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.

import os, sys
# waf imports
import Common, Params, gnome

# the following two variables are used by the target "waf dist"
VERSION='0.1.5'
APPNAME='gnome-packagekit'

# these variables are mandatory ('/' are converted automatically)
srcdir = '.'
blddir = '_build_'

def set_options(opt):
	pass

def configure(conf):
	conf.check_tool('gcc gnome')

	conf.check_pkg('packagekit', destvar='PACKAGEKIT', vnum='0.1.2', mandatory=True)
	conf.check_pkg('glib-2.0', destvar='GLIB', vnum='2.14.0', mandatory=True)
	conf.check_pkg('gtk+-2.0', destvar='GTK', vnum='2.10.0', mandatory=True)
	conf.check_pkg('libglade-2.0', destvar='LIBGLADE', vnum='2.5.0', mandatory=True)
	conf.check_pkg('dbus-1', destvar='DBUS', vnum='1.1.2', mandatory=True)
	conf.check_pkg('dbus-glib-1', destvar='DBUS_GLIB', vnum='0.73', mandatory=True)
	conf.check_pkg('gthread-2.0', destvar='GTHREAD', vnum='2.14.0', mandatory=True)
	conf.check_pkg('gconf-2.0', destvar='GCONF', vnum='0.22', mandatory=True)
	conf.check_pkg('libnotify', destvar='LIBNOTIFY', vnum='0.4.3', mandatory=True)

	conf.define('VERSION', VERSION)
	conf.define('GETTEXT_PACKAGE', 'gnome-packagekit')
	conf.define('PACKAGE', 'gnome-packagekit')


	conf.define('PACKAGE_DATADIR', conf.env['DATADIR'] + '/gnome-packagekit')
	conf.define('PACKAGE_DOCDIR', conf.env['DATADIR'] + '/share/doc/gnome-packagekit')
	conf.define('PACKAGE_LOCALEDIR', conf.env['DATADIR'] + '/locale')
	conf.env.append_value('CCFLAGS', '-DHAVE_CONFIG_H')

	conf.write_config_header('config.h')

def build(bld):
	# process subfolders from here
	bld.add_subdirs('src data po')

def shutdown():
	gnome.postinstall(APPNAME, schemas=True, icons=True, scrollkeeper=False)

def dist():
	# set the compression type to gzip (default is bz2)
	import Scripting
	Scripting.g_gz = 'gz'

	# after making the package, print the md5sum
	import md5
	from Scripting import DistTarball
	(f, filename) = DistTarball(APPNAME, VERSION)
	f = file(filename,'rb')
	m = md5.md5()
	readBytes = 1024 # read 1024 bytes per time
	while (readBytes):
		readString = f.read(readBytes)
		m.update(readString)
		readBytes = len(readString)
	f.close()
	print filename, m.hexdigest()
	sys.exit(0)

