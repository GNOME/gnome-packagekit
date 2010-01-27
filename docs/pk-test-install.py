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
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.

import os
import dbus
import pygtk
pygtk.require('2.0')
import gtk

class SessionInstall:

    def install_codec(self, widget, data=None):
        xid = widget.window.xid
        timespec = gtk.get_current_event_time()
        try:
            bus = dbus.SessionBus()
            proxy = bus.get_object('org.freedesktop.PackageKit','/org/freedesktop/PackageKit')
            iface = dbus.Interface(proxy, 'org.freedesktop.PackageKit')
            video = ("Windows Media Video 9 decoder", "gstreamer0.10(decoder-video/x-wma)(wmaversion=3)")
            audio = ("Windows Media Audio 9 decoder", "gstreamer0.10(decoder-audio/x-wma)(wmaversion=3)")
            codecs = (video, audio)
            iface.InstallGStreamerCodecs(xid,timespec,codecs)
        except Exception, e:
            print "Unable to install: %s" % str(e)

    def install_font(self, widget, data=None):
        xid = widget.window.xid
        timespec = gtk.get_current_event_time()
        try:
            bus = dbus.SessionBus()
            proxy = bus.get_object('org.freedesktop.PackageKit','/org/freedesktop/PackageKit')
            iface = dbus.Interface(proxy, 'org.freedesktop.PackageKit')
            iface.InstallFont(xid,timespec,"arial;example")
        except Exception, e:
            print "Unable to install: %s" % str(e)

    def install_mime_type(self, widget, data=None):
        xid = widget.window.xid
        timespec = gtk.get_current_event_time()
        try:
            bus = dbus.SessionBus()
            proxy = bus.get_object('org.freedesktop.PackageKit','/org/freedesktop/PackageKit')
            iface = dbus.Interface(proxy, 'org.freedesktop.PackageKit')
            iface.InstallMimeType(xid,timespec,"text/plain")
        except Exception, e:
            print "Unable to install: %s" % str(e)

    def use_helper(self, widget, data=None):
        xid = widget.window.xid

        codecs = ""
        codecs = codecs + "\"" + "gstreamer|0.10|totem|Windows Media Video 9 decoder|decoder-video/x-wmv, wmvversion=(int)3, fourcc=(fourcc)WVC1, format=(fourcc)WVC1" + "\""
        codecs = codecs + "\"" + "gstreamer|0.10|totem|Windows Media Audio 9 decoder|decoder-audio/x-wma, wmaversion=(int)3, bitrate=(int)192000, depth=(int)16" + "\""
        codecs = codecs + "\"" + "gstreamer|0.10|totem|FOOBAR protocol source|urisource-foobar (FOOBAR protocol source)" + "\""
        os.system("/home/hughsie/Desktop/gst-rpm-install --transient-for=%i %s" % (xid, codecs));

    def delete_event(self, widget, event, data=None):
        return False

    def destroy(self, widget, data=None):
        gtk.main_quit()

    def __init__(self):
        window = gtk.Window(gtk.WINDOW_TOPLEVEL)
        window.connect("delete_event", self.delete_event)
        window.connect("destroy", self.destroy)
        window.set_border_width(10)
        window.set_title("PackageKit Session Interface Tester")
        window.set_icon_name("help")

        hbox = gtk.VBox(homogeneous=True, spacing=9)

        button = gtk.Button("Install Codec")
        button.connect("clicked", self.install_codec, None)
        hbox.add(button)

        button = gtk.Button("Install Font")
        button.connect("clicked", self.install_font, None)
        hbox.add(button)

        button = gtk.Button("Install Mime Type")
        button.connect("clicked", self.install_mime_type, None)
        hbox.add(button)

        button = gtk.Button("Use helper")
        button.connect("clicked", self.use_helper, None)
        hbox.add(button)

        window.add(hbox);
        window.show_all()

    def main(self):
        gtk.main()

if __name__ == "__main__":
    install = SessionInstall()
    install.main()

