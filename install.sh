#!/usr/bin/env bash

if [ -n "$CHOICESPATH" ]; then
	CHOICESPATH=${HOME}/Choices:/usr/local/share/Choices:/usr/share/Choices
fi

function die () {
	echo "$*" >&2
	echo "Aborting..." >&2
	exit 1
}

# Create the directory if it doesn't exist. Exit if it can't be created.
function endir () {
	if [ ! -d "$1" ]; then
		mkdir "$1" || die "Can't create $1 directory!"
	fi
}

function confirm_or_die () {
	while [ 1 -eq 1 ]; do
		echo -n "[yes/no] >>> "
		read CONFIRM
		case $CONFIRM in
			[yY]*) return;;
			[nN]*) die "OK.";;
		esac
	done
}

cd `dirname $0`

./ROX-Filer/AppRun -v 2> /dev/null

if [ $? -ne 0 ]; then
	echo "Filer didn't run... attempting to compile..."
	./ROX-Filer/AppRun --compile
else
	echo "Filer is already compiled - good"
fi

echo Testing...
./ROX-Filer/AppRun -v
if [ $? -ne 0 ]; then
	die "Filer doesn't work! Giving up..."
fi

umask 022

cat << EOF


ROX-Filer comes with some icons and other defaults. These should be
installed where it can find them. By default, it looks in
	\${HOME}/Choices (${HOME}/Choices in your case)
	/usr/local/share/Choices
	/usr/share/Choices
in that order. You can choose a different search path by setting
the CHOICESPATH environment variable before running the filer.

Which Choices directory would you like to use? Option (1) is recommended
if you have root access; otherwise option (2) is best.

1) /usr/local/share/Choices
2) ${HOME}/Choices
3) /usr/share/Choices

Enter 1, 2, 3 or a path (starting with /):
EOF
echo -n ">>> "

read REPLY
echo

case $REPLY in
	1) CHOICES=/usr/local/share/Choices;;
	2) CHOICES=${HOME}/Choices;;
	3) CHOICES=/usr/share/Choices;;
	/*) CHOICES="$REPLY"
	    CHOICESPATH="${CHOICES}:${CHOICESPATH}"
	    echo "*** NOTE that if you install into a non-standard Choices "
	    echo "*** directory then you must ensure that CHOICESPATH is set "
	    echo "*** correctly before running the filer!"
	    echo
	    export CHOICESPATH;;
	*) die "Invalid choice!";;
esac

echo "Default icons and run actions will be installed into '$CHOICES'."

endir "$CHOICES"

if [ -d "$CHOICES/MIME-icons" ]; then
	cat << EOF
WARNING: You already have a $CHOICES/MIME-icons
directory --- any icons you have modified will be overwritten!

Continue? [y/n]
EOF
	echo -n ">>> "
	read REPLY
	case $REPLY in
		[yY]*) ;;
		*) die "OK, back up the ones you want and try again...";;
	esac
fi

endir "$CHOICES/MIME-icons"
endir "$CHOICES/MIME-info"
endir "$CHOICES/MIME-types"

echo "Installing icons..."
cp Choices/MIME-icons/* "$CHOICES/MIME-icons"

echo "Installing other files. If you haven't modified these since the "
echo "last installation then answer 'yes' to any questions about overwriting "
echo "files. Otherwise, you'll have to decide what to do!"
cp -i Choices/MIME-types/* "$CHOICES/MIME-types"
cp -i Choices/MIME-info/* "$CHOICES/MIME-info"
echo
echo "OK, done that. Next step..."

cat << EOF


Where would you like to install the filer itself?
Normally, you should choose (1) if you have root access, or (2) if not.

1) /usr/local/apps
2) ${HOME}/Apps
3) /usr/apps
4) Leave it where it is (it'll still work)

Enter 1, 2, 3, 4 or a path (starting with /):
EOF
echo -n ">>> "

read REPLY
echo

case $REPLY in
	1) APPDIR=/usr/local/apps;;
	2) APPDIR=${HOME}/Apps;;
	3) APPDIR=/usr/apps;;
	4) APPDIR="";;
	/*) APPDIR="$REPLY";;
	*) die "Invalid choice!";;
esac

if [ -n "$APPDIR" ]; then
	endir "$APPDIR"

	(cd ROX-Filer/src; make clean) > /dev/null
	if [ -e "$APPDIR/ROX-Filer" ]; then
		echo "ROX-Filer is already installed - delete the existing"
		echo "copy?"
		confirm_or_die
		echo Deleting...
		rm -rf "$APPDIR/ROX-Filer.old"
	fi
	cp -r ROX-Filer "$APPDIR"
else
	echo "OK, I'll leave it where it is."
	APPDIR=`pwd`
fi

echo Making ROX-Filer the default handler for directories...
rm "$CHOICES/MIME-types/special_directory" 2> /dev/null
ln -s "$APPDIR/ROX-Filer" "$CHOICES/MIME-types/special_directory"

cat << EOF


Where would you like to install the 'rox' script, which is used to run
the filer?

1) /usr/local/bin
2) ${HOME}/bin
3) /usr/bin
4) I don't want to install it

Enter 1, 2, 3, 4 or a path (starting with /):
EOF

echo -n ">>> "
read REPLY
echo

case $REPLY in
	1) BINDIR=/usr/local/bin;;
	2) BINDIR=${HOME}/bin;;
	3) BINDIR=/usr/bin;;
	4) BINDIR="";;
	/*) BINDIR="$REPLY";;
	*) die "Invalid choice!";;
esac

if [ -n "$BINDIR" ]; then
	endir "$BINDIR"

	cat > "$BINDIR/rox" << EOF
#!/bin/sh
exec $APPDIR/ROX-Filer/AppRun "\$@"
EOF
	[ $? -eq 0 ] || die "Failed to install 'rox' script"
	chmod a+x "$BINDIR/rox"

	cat << EOF
Script installed. You can run the filer by simply typing 'rox'
Make sure that $BINDIR is in your PATH though - if it isn't then
you must use
	\$ $BINDIR/rox
to run it instead.
EOF
else
	cat << EOF
OK, skipping installation of the 'rox' script.
To run the filer in future, use:

	\$ $APPDIR/ROX-Filer/AppRun.
EOF
fi

sleep 3
cat << EOF

	****************************
	*** Now read the manual! ***
	****************************

Run ROX and click on the information icon on the toolbar:

	\$ rox &

EOF
