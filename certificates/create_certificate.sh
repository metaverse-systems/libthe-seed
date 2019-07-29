#!/bin/bash

if [ -z $1 ];
then
  echo Usage:
  echo   ${0} certificate-name
  exit
fi

openssl req -x509 -newkey rsa:4096 -keyout key-${1}.pem -out cert-${1}.pem -days 365
