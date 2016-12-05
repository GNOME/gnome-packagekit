#!/bin/sh
# Copyright (C) 2016 Richard Hughes <richard@hughsie.com>
#
# Licensed under the GNU General Public License Version 2 or later

echo "Starting custom installation step"

if [ -z $MESON_INSTALL_PREFIX ]; then
    echo 'This is meant to be ran from Meson only!'
    exit 1
fi

PKGICONSDIR="${DESTDIR:-}${MESON_INSTALL_PREFIX}/share/gnome-packagekit/icons/hicolor";
ICONSDIR="${DESTDIR:-}${MESON_INSTALL_PREFIX}/share/icons/hicolor";
echo ${ICONSDIR}
mkdir -p ${ICONSDIR}/scalable/apps
mkdir -p ${ICONSDIR}/scalable/mimetypes
ln -fs ${PKGICONSDIR}/scalable/status/pk-package-sources.svg \
    ${ICONSDIR}/scalable/apps/gpk-repo.svg
ln -fs ${PKGICONSDIR}/scalable/status/pk-package-info.svg \
    ${ICONSDIR}/scalable/apps/gpk-log.svg
ln -fs ${PKGICONSDIR}/scalable/status/pk-update-high.svg \
    ${ICONSDIR}/scalable/apps/gpk-prefs.svg
ln -fs ${PKGICONSDIR}/scalable/status/pk-package-info.svg \
    ${ICONSDIR}/scalable/mimetypes/application-x-package-list.svg
ln -fs ${PKGICONSDIR}/scalable/status/pk-collection-installed.svg \
    ${ICONSDIR}/scalable/mimetypes/application-x-catalog.svg

echo "Finished custom install step"
