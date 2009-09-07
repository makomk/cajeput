#!/bin/bash
set +e

CAJ_GIT_REVISION=`git log HEAD^..HEAD|grep "^commit "|sed "s/commit //" `
CAJ_GIT_DATE=`git log HEAD^..HEAD|grep "^Date: "|sed "s/Date: //"|sed 's/^ *//' `

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
    