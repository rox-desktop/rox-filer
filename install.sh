#!/bin/sh

die() {
	echo "$*" >&2
	echo "Aborting..." >&2
	exit 1
}

# Create the directory if it doesn't exist. Exit if it can't be created.
endir() {
	if [ ! -d "$1" ]; then
		mkdir "$1" || die "Can't create $1 directory!"
	fi
}

confirm_or_die() {
	while [ 1 -eq 1 ]; do
		printf "[yes/no] >>> "
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

**************************************************************************

Where would you like to install the filer?
Normally, you should choose (1) if you have root access, or (2) if not.

1) /usr/local/apps
2) ${HOME}/Apps
3) /usr/apps
4) Leave it where it is (it'll still work)

Enter 1, 2, 3, 4 or a path (starting with /):
EOF
printf ">>> "

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
	if [ -f "$APPDIR/ROX-Filer" ]; then
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

cat << EOF


Where would you like to install the 'rox' script, which is used to run
the filer?

1) /usr/local/bin
2) ${HOME}/bin
3) /usr/bin
4) I don't want to install it

Enter 1, 2, 3, 4 or a path (starting with /):
EOF

printf ">>> "
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

Note: To run the filer in future, you must use:

	\$ $APPDIR/ROX-Filer/AppRun
EOF
fi

if [ ! -n "$CHOICESPATH" ]; then
	CHOICESPATH=${HOME}/Choices:/usr/local/share/Choices:/usr/share/Choices
fi

cat << EOF


ROX-Filer requires some icons and other defaults. These should be
installed where it can find them. By default, it looks in
	\${HOME}/Choices (${HOME}/Choices in your case)
	/usr/local/share/Choices
	/usr/share/Choices
in that order. You can choose a different search path by setting
the CHOICESPATH environment variable before running the filer. These
files are supplied in the 'rox-base' package. Make sure you have the
latest version installed!

EOF

IFS=":"
REPORT="I couldn't find the required files!"

for DIR in $CHOICESPATH; do
  if [ ! -n "$DIR" ]; then
    continue
  fi
  
  echo Looking for files in $DIR...

  if [ -f "$DIR/MIME-icons/special_executable.xpm" ]; then
    REPORT=""
    echo "Found them!"
    break
  elif [ -d "$DIR/MIME-icons" ]; then
    REPORT="I found an old installation in $DIR, but not the latest version."
  fi
done

if [ -n "$REPORT" ]; then
  echo
  echo $REPORT
  echo "Please download and install the latest version of the rox-base package"
  echo "from http://rox.sourceforge.net before using the filer!"
  echo
  echo "Continue?"
  confirm_or_die
fi

cat << EOF


	****************************
	*** Now read the manual! ***
	****************************

Run ROX and click on the information icon on the toolbar:

	\$ rox &
EOF
