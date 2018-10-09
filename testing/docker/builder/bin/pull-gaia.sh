#! /bin/bash -e

goanna_dir=$1
target=$2

gaia_repo=$(gaia_props.py $goanna_dir repository)
gaia_rev=$(gaia_props.py $goanna_dir revision)

tc-vcs checkout $target $gaia_repo $gaia_repo $gaia_rev
