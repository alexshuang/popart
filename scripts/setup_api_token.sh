#!/bin/sh

set -ex

pip install requests

pushd `mktemp -d`
git clone ssh://git@phabricator.sourcevertex.net/diffusion/ARTIFACTORYAPI/artifactory_api.git
cd ./artifactory_api
read -p "Input your account name of https://artifactory.sourcevertex.net [e.g. xxx@graphcore.ai]: " ARTIFACTORY_ACCOUNT
./setup_token.py --username $ARTIFACTORY_ACCOUNT
popd
