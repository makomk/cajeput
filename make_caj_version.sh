#!/bin/bash
set +e

# For exported copies of the source code
CAJ_GIT_REVISION='$Format:%H$'
CAJ_GIT_DATE='$Format:%ad$'

# Otherwise we get the live information from git itself.
[[ "$CAJ_GIT_REVISION" == \$* ]] && CAJ_GIT_REVISION=`git log HEAD^..HEAD --pretty=format:%H  `
[[ "$CAJ_GIT_DATE" == \$* ]] &&CAJ_GIT_DATE=`git log HEAD^..HEAD --pretty=format:%ad `

cp caj_version.h.in caj_version.h.new
sed -i "s/%CAJ_GIT_REVISION%/$CAJ_GIT_REVISION/g" caj_version.h.new
sed -i "s/%CAJ_GIT_DATE%/$CAJ_GIT_DATE/g" caj_version.h.new

if [ -e caj_version.h ]; then
    if diff caj_version.h caj_version.h.new > /dev/null; then
	echo "No changes to caj_version.h"
	rm caj_version.h.new
    else
	mv caj_version.h.new caj_version.h
	echo "caj_version.h updated"
    fi
else
    mv caj_version.h.new caj_version.h
fi
    
