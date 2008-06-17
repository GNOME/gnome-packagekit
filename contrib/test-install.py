import dbus

try:
    bus = dbus.SessionBus()
except dbus.DBusException, e:
    print 'Unable to connect to dbus: %s' % str(e)
    sys.exit()
try:
    proxy = bus.get_object('org.freedesktop.PackageKit', '/org/freedesktop/PackageKit')
    iface = dbus.Interface(proxy, 'org.freedesktop.PackageKit')
    iface.InstallPackageName('openoffice-clipart')
except dbus.DBusException, e:
    print 'Unable to use PackageKit: %s' % str(e)

