#!/bin/sh

die() {
	echo "$*" >&2
	echo "Aborting..." >&2
	exit 1
}

get_dir() {
	while [ 1 -eq 1 ]; do
		printf "[$1] >>> "
		read DIR

		case "$DIR" in
			/*) return;;
			?*) echo "The path must start with a \`/'";;
			*) DIR="$1"
			   echo OK, using default value.
			   return;;
		esac
	done
}

# Create the directory if it doesn't exist. Exit if it can't be created.
endir() {
	if [ ! -d "$1" ]; then
		mkdir -p "$1" || die "Can't create $1 directory!"
	fi
}

confirm_or_die() {
	while [ 1 -eq 1 ]; do
		printf "[yes/no] >>> "
		read CONFIRM
		case "$CONFIRM" in
			[yY]*) return;;
			[nN]*) die "OK.";;
		esac
	done
}

cd `dirname $0`

echo Testing the filer...
./ROX-Filer/AppRun -v
if [ $? -ne 0 ]; then
	die "Filer doesn't work! Giving up..."
fi

umask 022

cat << EOF

**************************************************************************

Where would you like to install the filer?
Normally, you should choose (1) if you have root access, or (2) if not.

1) Inside /usr/local
2) Inside my home directory
3) Inside /usr
4) Specify paths manually

Enter 1, 2, 3 or 4:
EOF
printf ">>> "

read REPLY
echo

case $REPLY in
	1) APPDIR=/usr/local/apps
	   BINDIR=/usr/local/bin
	   MANDIR=/usr/local/man
	   ;;
	2) APPDIR=${HOME}/Apps
	   BINDIR=${HOME}/bin
	   if [ ! -d ${HOME}/man ]; then
		MANDIR=""
	   else
		MANDIR=${HOME}/man
	   fi
	   ;;
	3) APPDIR=/usr/apps
	   BINDIR=/usr/bin
	   MANDIR=/usr/man
	   ;;
	4) echo "Where should the ROX-Filer application go?"
	   get_dir "/usr/local/apps"
	   APPDIR="$DIR"
	   echo
	   echo "Where should the launcher script go?"
	   get_dir "/usr/local/bin"
	   BINDIR="$DIR"
	   echo
	   echo "Where should the manual page go?"
	   get_dir "/usr/local/man"
	   MANDIR="$DIR"
	   ;;
	*) die "Invalid choice!";;
esac

cat << EOF

The application directory will be:
	$APPDIR/ROX-Filer

The launcher script will be:
	$BINDIR/rox

EOF
if [ -n "$MANDIR" ]; then
	echo "The manual pages will be:"
	echo "	$MANDIR/man1/rox.1"
	echo "	$MANDIR/man1/ROX-Filer.1"
else
	echo "The manual page will not be installed."
fi

echo
echo "OK?"
confirm_or_die
echo

if [ -n "$MANDIR" ]; then
	echo "Installing manpage..."
	endir "$MANDIR"
	endir "$MANDIR/man1"
	cp rox.1 "$MANDIR/man1/rox.1" || die "Can't install manpage!"
	rm -f "$MANDIR/man1/ROX-Filer.1" || die "Can't install manpage!"
	ln -s "$MANDIR/man1/rox.1" "$MANDIR/man1/ROX-Filer.1" || die "Can't install manpage!"
fi

echo "Installing application..."
endir "$APPDIR"

(cd ROX-Filer/src; make clean) > /dev/null
if [ -d "$APPDIR/ROX-Filer" ]; then
	echo "ROX-Filer is already installed - delete the existing"
	echo "copy?"
	confirm_or_die
	echo Deleting...
	rm -rf "$APPDIR/ROX-Filer"
fi
cp -r ROX-Filer "$APPDIR"

echo "Installing launcher script..."
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

	\$ rox
EOF
