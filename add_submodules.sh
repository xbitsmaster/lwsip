#!/bin/bash

# Remove proxy settings
unset http_proxy https_proxy HTTP_PROXY HTTPS_PROXY

# Configure git
git config --global http.version HTTP/1.1
git config --global http.postBuffer 524288000

# Add submodules
git submodule add https://github.com/ireader/sdk.git 3rds/sdk
git submodule add https://github.com/ireader/media-server.git 3rds/media-server
git submodule add https://github.com/ireader/avcodec.git 3rds/avcodec
git submodule add https://github.com/ireader/3rd.git 3rds/3rd
git submodule add https://github.com/lwip-tcpip/lwip.git 3rds/lwip
git submodule add https://github.com/Mbed-TLS/mbedtls.git 3rds/mbedtls
git submodule add https://github.com/pjsip/pjproject.git 3rds/pjsip

echo "Done adding submodules"
