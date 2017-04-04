#!/bin/sh
echo "Starting custom installation step"

if [ -z $MESON_INSTALL_PREFIX ]; then
    echo 'This is meant to be ran from Meson only!'
    exit 1
fi

PKGICONSDIR="../../../../gnome-packagekit/icons/hicolor";
ICONSDIR="${DESTDIR:-}${MESON_INSTALL_PREFIX}/share/icons/hicolor";
echo ${ICONSDIR}
mkdir -p ${ICONSDIR}/scalable/apps
mkdir -p ${ICONSDIR}/scalable/mimetypes

cd ${ICONSDIR}/scalable/apps
ln -sf ${PKGICONSDIR}/scalable/status/pk-package-sources.svg gpk-repo.svg
ln -sf ${PKGICONSDIR}/scalable/status/pk-package-info.svg gpk-log.svg
ln -sf ${PKGICONSDIR}/scalable/status/pk-update-high.svg gpk-prefs.svg
ln -sf ${PKGICONSDIR}/scalable/status/pk-package-sources.svg gpk-repo.svg
cd -

cd ${ICONSDIR}/scalable/mimetypes
ln -sf ${PKGICONSDIR}/scalable/status/pk-package-info.svg application-x-package-list.svg
ln -sf ${PKGICONSDIR}/scalable/status/pk-collection-installed.svg application-x-catalog.svg
cd -

echo "Finished custom install step"
