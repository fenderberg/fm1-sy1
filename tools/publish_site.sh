#!/bin/sh
# Build SY1's site: a release of VERSION, the browser emulator, fm1-base's pages (front page, installer, manual)
# and the Patch Manager, in app/build/site. With --push it replaces the gh-pages branch of origin with it.
#   tools/publish_site.sh VERSION [--push]
set -e
cd "$(dirname "$0")/.."
V="${1:?usage: tools/publish_site.sh VERSION [--push]}"
PY="${PYTHON:-python3}"
./build.sh --release "$V"
./build.sh --web --release "$V"
"$PY" base/tools/make_site.py app "app/build/sy1-$V.fwsc" "$V" app/build/site
mkdir -p app/build/site/manager
cp app/site/manager/index.html app/build/site/manager/index.html
touch app/build/site/.nojekyll
echo "site: app/build/site"
[ "${2:-}" = "--push" ] || exit 0
REMOTE="$(git remote get-url origin)"
SHA="$(git rev-parse --short HEAD)"
cd app/build/site
rm -rf .git
git init -q -b gh-pages
git add -A
git -c user.name="$(git -C ../../.. log -1 --format=%an)" -c user.email="$(git -C ../../.. log -1 --format=%ae)" \
    commit -q -m "Site for SY1 $V (from $SHA)"
git push -q -f "$REMOTE" gh-pages
rm -rf .git
echo "pushed the site for SY1 $V to gh-pages"
