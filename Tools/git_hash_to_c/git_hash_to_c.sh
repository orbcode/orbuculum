#!/bin/bash
head_hash=$(git rev-parse HEAD)
short_head_hash=${head_hash:0:8}
head_branch=$(git rev-parse --abbrev-ref HEAD)
build_date=$(date "+%Y-%m-%d %H:%M:%S%z")

if [[ $(git diff --shortstat 2> /dev/null | tail -n1) = "" ]]; then
    echo "#define GIT_DIRTY         0"
else
    echo "#define GIT_DIRTY         1"
fi

echo "#define GIT_HASH          0x$short_head_hash"
echo "#define GIT_BRANCH        \"$head_branch\""  
echo "#define BUILD_DATE        \"$build_date\""  

