# Copyright (c) 2009-2010 Aidan Thornton, all rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#    * Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in the
#      documentation and/or other materials provided with the distribution.
#
# THIS SOFTWARE IS PROVIDED BY AIDAN THORNTON ''AS IS'' AND ANY
# EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
# DISCLAIMED. IN NO EVENT SHALL AIDAN THORNTON BE LIABLE FOR ANY
# DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
# (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
# LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
# ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

# Creates release tarballs for Cajeput automatically.
# First make a tag with the name of the appropriate version (if you don't, 
# it'll fail with "fatal: Not a valid object name"

#!/bin/bash
set -e

CAJ_VERSION="$1"

if [[ "$CAJ_VERSION" == "" ]]; then
    echo "Usage: $0 <version>"
    exit 1
fi

CAJ_PREFIX="cajeput-$CAJ_VERSION"
CAJ_TARBALL="$CAJ_PREFIX.tar"
CAJ_TARBALL_BZ2="$CAJ_TARBALL.bz2"

if [ -e "$CAJ_TARBALL" ]; then
    echo "Error: $CAJ_TARBALL already exists"
    exit 1
fi

if [ -e "$CAJ_TARBALL_BZ" ]; then
    echo "Error: $CAJ_TARBALL_BZ2 already exists"
    exit 1
fi

if ! git archive --prefix="$CAJ_PREFIX"/ -o "$CAJ_TARBALL" "$CAJ_VERSION"; then
    echo "Error: Couldn't create archive."
    echo 'Did you remember to tag the new release using "git tag"?'
    exit 1
fi
bzip2 "$CAJ_TARBALL"

echo "Created $CAJ_TARBALL_BZ2"