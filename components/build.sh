#!/bin/bash

if [ "$1" == "linux" ];
then
## Linux
CONFIGURE_COMMAND="./configure"
fi

if [ "$1" == "windows" ];
then
## Windows
export PKG_CONFIG_PATH=/usr/x86_64-w64-mingw32/lib/pkgconfig/
export PKG_CONFIG=/usr/bin/pkg-config
CONFIGURE_COMMAND="./configure --host=x86_64-w64-mingw32 --prefix=/usr/x86_64-w64-mingw32"
fi

for d in `ls`;
do
  if [ "${d}" == "create_component.sh" ] || [ "${d}" == "build.sh" ] || [ "${d}" == "skeleton_component" ] || [ "${d}" == "sign.sh" ];
  then
    continue;
  fi
  echo $d;
  cd $d
  make distclean
  `${CONFIGURE_COMMAND}`
  make
  cd ..
done
