#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
revision=b0ed3e9a0e5cb598dc5c93f22e5f159de80d5d95
if [ ! -d .external/BedrockTools/.git ]; then
    git clone https://github.com/QYCottage/BedrockTools.git .external/BedrockTools
fi
git -C .external/BedrockTools checkout --detach "$revision"
