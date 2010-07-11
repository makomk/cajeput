#!/bin/bash
set +e

# For exported copies of the source code
CAJ_GIT_REVISION='$Format:%H$'
CAJ_GIT_DATE='$Format:%ad$'

# Otherwise we get the live information from git itself.
[[ "$CAJ_GIT_REVISION" == \$* ]] && CAJ_GIT_REVISION=`git log HEAD^..HEAD --pretty=format:%H  `
[[ "$CAJ_GIT_DATE" == \$* ]] &&CAJ_GIT_DATE=`git log HEAD^..HEAD --pretty=format:%ad `

cp caj_version.c.in caj_version.c.new
sed -i "s/%CAJ_GIT_REVISION%/$CAJ_GIT_REVISION/g" caj_version.c.new
sed -i "s/%CAJ_GIT_DATE%/$CAJ_GIT_DATE/g" caj_version.c.new

if [ -e caj_version.c ]; then
    if diff caj_version.c caj_version.c.new > /dev/null; then
	echo "No changes to caj_version.c"
	rm caj_version.c.new
    else
	mv caj_version.c.new caj_version.c
	echo "caj_version.c updated"
    fi
else
    mv caj_version.c.new caj_version.c
fi
    
