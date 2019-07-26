#!/bin/bash

CONFIGURE_COMMAND="./configure"

for d in `ls`;
do
  if [ "${d}" == "create_component.sh" ] || [ "${d}" == "build.sh" ] || [ "${d}" == "skeleton_component" ];
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
