#!/usr/bin/python
# Licensed under the GNU General Public License Version 2
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.

import dbus
import pygtk
pygtk.require('2.0')
import gtk

class CodecInstall:

    def install(self, widget, data=None):
        bus = dbus.SessionBus()
        try:
            proxy = bus.get_object('org.freedesktop.PackageKit','/org/freedesktop/PackageKit')
            iface = dbus.Interface(proxy, 'org.freedesktop.PackageKit')

            xid = widget.window.xid

            #xid = 0
            timespec = gtk.get_current_event_time()
            video = ("Windows Media Video 9 decoder", "gstreamer0.10(decoder-video/x-wma)(wmaversion=3)")
            audio = ("Windows Media Audio 9 decoder", "gstreamer0.10(decoder-audio/x-wma)(wmaversion=3)")
            codecs = (video, audio)
            print "xid",xid
            print "timespec",timespec
            print "codecs",codecs
            iface.InstallGStreamerCodecs(xid,timespec,codecs)
        except Exception, e:
            print "Unable to send install codec: %s" % str(e)

    def delete_event(self, widget, event, data=None):
        return False

    def destroy(self, widget, data=None):
        gtk.main_quit()

    def __init__(self):
        self.window = gtk.Window(gtk.WINDOW_TOPLEVEL)
        self.window.connect("delete_event", self.delete_event)
        self.window.connect("destroy", self.destroy)
        self.window.set_border_width(10)
        self.button = gtk.Button("Install Codec")
        self.button.connect("clicked", self.install, None)
        self.window.add(self.button)
        self.button.show()
        self.window.show()

    def main(self):
        gtk.main()

if __name__ == "__main__":
    install = CodecInstall()
    install.main()

