#!/bin/bash

if [ -z "$1" ] || [ -z "$2" ]
then
    printf "Usage:\n"
    printf "    $0 new_system_name author@email.com\n\n"
    printf "Name cannot contain spaces\n"
    exit
fi

cp -r skeleton_system ${1}
cd ${1}
sed -i "s/SKELETON/${1}/g" configure.ac
sed -i "s/AUTHOR_EMAIL/${2}/g" configure.ac
sed -i "s/AUTHOR_EMAIL/${2}/g" AUTHORS
sed -i "s/AUTHOR_EMAIL/${2}/g" COPYING

sed -i "s/SKELETON/${1}/g" src/Makefile.am
sed -i "s/SKELETON/${1}/g" src/SKELETON.hpp
sed -i "s/SKELETON/${1}/g" src/SKELETON.cpp

mv src/SKELETON.hpp src/${1}.hpp
mv src/SKELETON.cpp src/${1}.cpp

printf "Your system is in ./${1}\n"
printf "To compile:\n"
printf "    cd ${1}\n"
printf "    ./autogen.sh\n"
printf "    ./configure\n"
printf "    make\n"
