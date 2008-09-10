# treat PackageKit git as the source for updated images
for image in *.png; do
	echo "updating $image"
	cp ../../../../PackageKit/docs/html/img/$image .
done

