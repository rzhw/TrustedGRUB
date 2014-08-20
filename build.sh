#!/bin/bash

VERBOSE=" /dev/null"
BUILD=1
CONFIGURE=1

export CC

configure_tgrub()
{
    echo "- Configuring TrustedGRUB"
    if [[ $(which aclocal) = "" ]] ; then
        echo "Need automake and autoconf"
        exit -1
    else
        aclocal >& $VERBOSE
        if [ $? != 0 ]; then exit 501; fi
        autoconf >& $VERBOSE
        if [ $? != 0 ]; then exit 502; fi
        automake >& $VERBOSE
        if [ $? != 0 ]; then exit 503; fi
        if [[ $SHOWSHA1 ]] ; then
            ./configure CFLAGS="-DSHOW_SHA1" >& $VERBOSE
        else
	    ./configure >& $VERBOSE
        fi
        if [ $? != 0 ]; then exit 504; fi
    fi
}

build_tgrub()
{
    echo "- Compiling TrustedGRUB"
    gcc util/create_sha1.c -o util/create_sha1
    if [ $? != 0 ]; then exit 601; fi
    gcc util/verify_pcr.c -o util/verify_pcr
    if [ $? != 0 ]; then exit 602; fi
    make >& $VERBOSE 
    if [ $? != 0 ]; then exit 603; fi
    chmod g+w * -R
    if [ $? != 0 ]; then exit 604; fi
    chmod a+x util/grub-install
    if [ $? != 0 ]; then exit 605; fi
}

until [ -z "$1" ]; do
    case $1 in

"-h" | "--help")
    echo "Script to build TrustedGRUB."
    echo "The following options are possible:"
    echo ""
    echo "-h  | --help        : show this help"
    echo "-v  | --verbose     : compile with verbose output"
    echo "-nb | --nobuild     : do not build"
    echo "-nc | --noconfigure : do not configure"
    echo "-s  | --showsha1    : compile TrustedGRUB with \"-DSHOW_SHA1\""
    exit 0
;;

"-s" | "--showsha1")
    shift;
    SHOWSHA1=1
;;

"-v" | "--verbose")
    shift;
    echo "Enabling verbose output"
    VERBOSE=" /dev/stdout"
;;

"-nb" | "--nobuild")
    shift;
    BUILD=0
;;

"-nc" | "--noconfigure")
    shift;
    CONFIGURE=0
;;

*)
    shift;
;;

    esac
done

if [ "$CONFIGURE" == "1" ] ; then
    configure_tgrub
fi

if [ "$BUILD" == "1" ] ; then
    build_tgrub
    set -e
    echo "- Copying output to bin/ folder ..."
    mkdir -p bin
    cp stage1/stage1 bin/
    cp stage2/stage2 bin/
    cp grub/grub bin/
    cp menu.lst bin/
    cp trustedgrub_install.sh bin/
fi

echo "- Done!"
